#include "save_state_service.hpp"
#include "save_state_codec.hpp"
#include "save_state_memory.hpp"
#include "save_state_overlays.hpp"
#include "audio_host.hpp"
#include "librecomp/state.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>

namespace twine::state {
namespace {
namespace runtime = ultramodern::state;
std::filesystem::path directory;
std::atomic_bool busy{false};
std::atomic<uint64_t> last_completion{0};
Slot active_slot = Slot::Manual;
std::mutex notification_mutex;
std::atomic_bool notification_pending{false};
std::string notification_title, notification_message, transaction_warning;

void notify(const char* title, const std::string& message) {
    std::lock_guard lock(notification_mutex);
    notification_title = title; notification_message = message;
    notification_pending.store(true, std::memory_order_release);
}
Bytes encode_timers(const runtime::TimerState& saved) {
    Writer out; out.u32(1); out.fields(saved.counter, saved.os_time);
    out.u32(uint32_t(saved.active.size()));
    for (auto timer : saved.active) out.u32(timer);
    return std::move(out.bytes);
}
runtime::TimerState decode_timers(const Bytes& bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Unsupported timer state schema");
    runtime::TimerState saved; in.fields(saved.counter, saved.os_time);
    const uint32_t count = in.u32();

    if (count > state_rdram_size / sizeof(uint32_t) || count > (bytes.size() - 24) / 4)
        throw std::runtime_error("Invalid saved timer count");
    saved.active.reserve(count);
    for (uint32_t i = 0; i < count; ++i) saved.active.push_back(in.u32());
    in.end(); return saved;
}
Bytes encode_events(const runtime::VideoState& v, const std::vector<runtime::ExternalMessageState>& messages) {
    Writer out; out.u32(1); out.fields(v.current, v.field, v.remaining_retraces, v.sequence);
    for (const auto& f : v.frames) out.fields(f.mode, f.framebuffer, f.queue, f.message, f.state, f.control, f.retraces);
    for (auto r : v.registers) out.u32(r);
    for (auto r : v.screen_registers) out.u32(r);
    for (auto r : v.events) out.scalar(r);
    out.u32(uint32_t(messages.size()));
    for (auto m : messages) out.fields(m.queue, m.message, m.jam, m.reliable);
    return std::move(out.bytes);
}
struct EventState {
    runtime::VideoState video;
    std::vector<runtime::ExternalMessageState> messages;
};
EventState decode_events(const Bytes& bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Unsupported event state schema");
    EventState saved; auto& v = saved.video;
    in.fields(v.current, v.field, v.remaining_retraces, v.sequence);
    for (auto& f : v.frames) in.fields(f.mode, f.framebuffer, f.queue, f.message, f.state, f.control, f.retraces);
    for (auto& r : v.registers) r = in.u32();
    for (auto& r : v.screen_registers) r = in.u32();
    for (auto& r : v.events) r = in.scalar<int32_t>();
    const uint32_t count = in.u32();
    if (count > 65536) throw std::runtime_error("Invalid saved pending message count");
    saved.messages.resize(count);
    for (auto& m : saved.messages) in.fields(m.queue, m.message, m.jam, m.reliable);
    in.end(); return saved;
}
Bytes encode_media(const recomp::state::SaveMediaState& saved) {
    Writer out; out.u32(1); out.u32(saved.type); out.u32(uint32_t(saved.bytes.size()));
    for (auto b : saved.bytes) out.scalar(uint8_t(b));
    for (auto b : saved.flash_page) out.scalar(uint8_t(b));
    for (auto h : saved.handles) out.u32(h);
    return std::move(out.bytes);
}
recomp::state::SaveMediaState decode_media(const Bytes& bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Unsupported save-media state schema");
    recomp::state::SaveMediaState saved;
    saved.type = in.u32(); const uint32_t count = in.u32();
    if (count > 1024 * 1024) throw std::runtime_error("Invalid saved media size");
    saved.bytes.resize(count);
    for (auto& b : saved.bytes) b = char(in.scalar<uint8_t>());
    for (auto& b : saved.flash_page) b = char(in.scalar<uint8_t>());
    for (auto& h : saved.handles) h = in.u32();
    in.end(); return saved;
}

void transaction(Action action, uint8_t* rdram, const Roots& roots) {
    transaction_warning.clear();
    const auto path = directory / (active_slot == Slot::Manual ? "manual.twinestate" : "checkpoint.twinestate");
    std::optional<Image> loaded;
    if (action == Action::Restore) loaded.emplace(read_file(path));

    std::optional<PreparedMemory> prepared_memory;
    runtime::VideoPause video_pause;
    runtime::TimerPause timer_pause;
    runtime::retire_audio_dma();
    AudioPause audio_pause;
    recomp::state::SaveMediaPause media_pause(rdram);
    struct Transaction {
        Action action;
        uint8_t* rdram;
        const Roots& roots;
        const std::filesystem::path& path;
        std::optional<Image>& loaded;
        std::optional<PreparedMemory>& memory;
        runtime::TimerPause& timer;
        AudioPause& audio;
        recomp::state::SaveMediaPause& media;
        ultramodern::renderer::RendererContext* renderer = nullptr;
    } t{action, rdram, roots, path, loaded, prepared_memory, timer_pause, audio_pause, media_pause};
    runtime::run_renderer_operation([](ultramodern::renderer::RendererContext& renderer, void* opaque) {
        auto& t = *static_cast<Transaction*>(opaque);
        t.renderer = &renderer;
        renderer.run_state_operation([](void* opaque) {
            auto& t = *static_cast<Transaction*>(opaque);
            const auto bindings = runtime::thread_bindings();
            const std::span<const uint8_t> current(t.rdram, state_rdram_size);
            if (t.action == Action::Save) {
                Image image; image.source = parse_fingerprint(TWINE_SOURCE_FINGERPRINT);
                const auto pending = runtime::capture_external_messages();
                const auto video = runtime::capture_video();
                runtime::validate_video(video, current);
                runtime::PreparedExternalMessages validate_messages(pending, current, bindings);
                auto memory = capture_memory(current, bindings, t.roots);
                image[Section::Memory] = std::move(memory.memory);
                image[Section::Threads] = std::move(memory.threads);
                image[Section::Overlays] = capture_overlays();
                image[Section::Timers] = encode_timers(t.timer.captured());
                image[Section::Events] = encode_events(video, pending);
                image[Section::Audio] = t.audio.capture();
                image[Section::Renderer] = t.renderer->capture_state();
                const auto& game = captured_game_state();
                image[Section::Enhancements] = game.enhancements;
                image[Section::Modernization] = game.modernization;
                image[Section::SaveMedia] = encode_media(t.media.capture());
                const auto result = write_file(t.path, image);
                transaction_warning = result.warning;

                return;
            }
            const Image& image = *t.loaded;
            t.memory.emplace(prepare_memory({image[Section::Memory], image[Section::Threads]}, current, bindings, t.roots));
            const auto& saved_memory = t.memory->memory;
            const auto events = decode_events(image[Section::Events]);
            runtime::validate_video(events.video, saved_memory);
            runtime::PreparedExternalMessages messages(events.messages, saved_memory, bindings);
            PreparedOverlays overlays(image[Section::Overlays]);
            t.timer.prepare(decode_timers(image[Section::Timers]), saved_memory);
            t.audio.prepare(image[Section::Audio]);
            t.media.prepare(decode_media(image[Section::SaveMedia]));
            stage_restore_roots(t.memory->roots);
            stage_restore_game(t.rdram, {image[Section::Enhancements], image[Section::Modernization]});
            auto renderer = t.renderer->prepare_state(image[Section::Renderer]);

            std::memcpy(t.rdram, saved_memory.data(), saved_memory.size());
            overlays.commit(); messages.commit(); runtime::restore_video(events.video);
            t.media.commit(); t.audio.commit(); t.timer.commit(); renderer->commit();

        }, opaque);
    }, &t);
}
void complete(Action action, const char* error) noexcept {
    const uint64_t next = (last_completion.load(std::memory_order_relaxed) >> 2) + 1;
    last_completion.store((next << 2) | (uint64_t(action == Action::Restore) << 1) |
        uint64_t(error == nullptr), std::memory_order_release);
    try {
        if (error) {

            notify(action == Action::Save ? "State not saved" : "State not restored", error);
        } else if (!transaction_warning.empty()) {
            notify("State saved with a warning", transaction_warning);
        } else {
            notify(action == Action::Save ? "State saved" : "State restored",
                active_slot == Slot::Manual ? "Manual state — persistent across game restarts." : "Checkpoint state.");
        }
    } catch (...) {

    }
    busy.store(false, std::memory_order_release);
}
bool submit(Action action, Slot slot) {
    if (directory.empty() || (slot != Slot::Manual && slot != Slot::Checkpoint)) return false;
    bool expected = false;
    if (!busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    active_slot = slot;
    if (!request(action)) { busy.store(false, std::memory_order_release); return false; }
    return true;
}
}
void initialize(const std::filesystem::path& configuration_directory) {
    if (configuration_directory.empty()) throw std::invalid_argument("Missing save-state directory");
    directory = configuration_directory / "states";
    initialize_rendezvous(transaction, complete);
}
bool save(Slot slot) { return submit(Action::Save, slot); }
bool restore(Slot slot) { return submit(Action::Restore, slot); }
OperationStatus operation_status() {
    const auto value = last_completion.load(std::memory_order_acquire);
    return {value >> 2, bool(value & 2), bool(value & 1)};
}
bool take_notification(std::string& title, std::string& message) {
    if (!notification_pending.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(notification_mutex);
    if (!notification_pending.exchange(false, std::memory_order_acq_rel)) return false;
    title.swap(notification_title); message.swap(notification_message);
    return true;
}
}
