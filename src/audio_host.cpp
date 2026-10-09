#include "audio_host.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SDL.h"
#include "audio_pipeline.hpp"
#include "audio_state_stream.hpp"
#include "save_state_codec.hpp"
#include "ultramodern/state.hpp"
#include "librecomp/rsp.hpp"
#include "recompui/config.h"
#include "ultramodern/ultramodern.hpp"

namespace twine::audio_host {
namespace {

SDL_AudioDeviceID device = 0;
std::unique_ptr<twine::audio::StateStream> stream;
twine::audio::OutputQueue output;
std::atomic_bool state_audio_paused{false};
uint32_t input_frequency = 48000;
uint32_t output_frequency = 0;
uint16_t device_buffer_frames = 0;
size_t prebuffer_target_frames = 0;
bool playback_started = false;
std::atomic_bool resume_reopen_pending{false};
std::atomic_bool device_event_pending{false};
std::atomic_bool reconfiguring_device{false};
std::atomic<SDL_AudioDeviceID> watched_device{0};
std::atomic<DeviceStatus> status{DeviceStatus::Stopped};
bool watching_events = false;
bool owns_audio_subsystem = false;
std::chrono::steady_clock::time_point next_resume_reopen{};
std::mutex mutex;

std::mutex producer_mutex;
std::condition_variable output_consumed;
std::mutex lifecycle_mutex, wake_mutex;
std::condition_variable wake_device;
std::thread device_worker;
std::atomic_bool stop_worker{false};
std::string hardware_error;
std::chrono::steady_clock::time_point silent_clock;
twine::audio::SilentFrameClock silent_frames;
std::atomic<uint64_t> callback_progress{0};
uint64_t observed_callback_progress = 0;
std::chrono::steady_clock::time_point last_callback_progress;

void advance_silent_device_locked() {
    if (device || !stream || state_audio_paused) return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - silent_clock).count();
    const uint64_t frames = silent_frames.advance(uint64_t(std::max(int64_t{0}, elapsed)));
    output.discard(size_t(std::min<uint64_t>(frames, twine::audio::OutputQueue::max_samples / 2)) * 2);
    silent_clock = now;
}

void close_device_locked() {
    watched_device.store(0, std::memory_order_release);
    if (device != 0) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    playback_started = false;
}

void close_locked() {
    close_device_locked();
    stream.reset();
    output.clear();
    output_frequency = 0;
}

void append_output_locked(std::span<const int16_t> samples, std::unique_lock<std::mutex>& lock) {
    while (!samples.empty() && stream) {
        advance_silent_device_locked();
        const size_t count = std::min(samples.size(), twine::audio::OutputQueue::max_samples - output.size());
        if (count) {
            output.append(samples.first(count));
            samples = samples.subspan(count);
        }
        if (!samples.empty()) {

            output_consumed.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
}

bool set_frequency_locked(uint32_t frequency, std::unique_lock<std::mutex>& lock) {
    if (frequency == 0 ||
        frequency > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    try {
        if (stream == nullptr || frequency != input_frequency) {
            auto replacement = std::make_unique<twine::audio::StateStream>(frequency, output_frequency);
            if (stream != nullptr) {
                twine::audio::Samples tail;
                stream->finish(tail);
                append_output_locked(tail, lock);

            }
            stream = std::move(replacement);
        }
    } catch (const std::exception& error) {
        SDL_SetError("%s", error.what());
        return false;
    }
    input_frequency = frequency;
    return true;
}

bool open_device() {
    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(twine::audio::output_frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 256;
    desired.callback = [](void*, Uint8* bytes, int count) {
        const size_t samples = size_t(count) / sizeof(int16_t);

        output.consume({reinterpret_cast<int16_t*>(bytes), samples});
        callback_progress.fetch_add(1, std::memory_order_relaxed);
        output_consumed.notify_one();

    };

    SDL_AudioSpec obtained{};
    const auto opened = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (opened == 0) {
        return false;
    }
    std::lock_guard lock(mutex);
    advance_silent_device_locked();
    device = opened;
    output_frequency = static_cast<uint32_t>(obtained.freq);
    device_buffer_frames = obtained.samples;
    SDL_PauseAudioDevice(device, 1);
    watched_device.store(device, std::memory_order_release);
    prebuffer_target_frames = twine::audio::prebuffer_target(
        output_frequency,
        device_buffer_frames);
    observed_callback_progress = callback_progress.load(std::memory_order_relaxed);
    last_callback_progress = std::chrono::steady_clock::now();
    if (!playback_started && output.size() / 2 >= prebuffer_target_frames) {
        playback_started = true;
        SDL_PauseAudioDevice(device, 0);
    }
    return true;
}

bool initialize_driver() {
    struct Reconfiguration {
        Reconfiguration() { reconfiguring_device = true; }
        ~Reconfiguration() { reconfiguring_device = false; }
    } reconfiguration;

    if (!owns_audio_subsystem) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            hardware_error = SDL_GetError();
            return false;
        }
        owns_audio_subsystem = true;
    }
    if (!open_device()) {
        hardware_error = SDL_GetError();
        return false;
    }

    const char* active_driver = SDL_GetCurrentAudioDriver();

    return true;
}

void detach_device() {
    SDL_AudioDeviceID detached;
    {
        std::lock_guard lock(mutex);
        advance_silent_device_locked();
        detached = device;
        if (detached) SDL_PauseAudioDevice(detached, 1);
        device = 0;
        watched_device = 0;
        playback_started = false;
        silent_clock = std::chrono::steady_clock::now();
        silent_frames = {};
    }

    if (detached) SDL_CloseAudioDevice(detached);
}

void schedule_resume_retry_locked() {
    resume_reopen_pending = true;
    next_resume_reopen =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void use_silent_output() {
    status.store(DeviceStatus::Unavailable, std::memory_order_release);
    schedule_resume_retry_locked();

}

int SDLCALL watch_device(void*, SDL_Event* event) {
    if (reconfiguring_device.load(std::memory_order_acquire)) return 1;
    if ((event->type == SDL_AUDIODEVICEADDED || event->type == SDL_AUDIODEVICEREMOVED) &&
            !event->adevice.iscapture) {
        const bool relevant = event->type == SDL_AUDIODEVICEADDED
            ? status.load(std::memory_order_acquire) == DeviceStatus::Unavailable
            : event->adevice.which == watched_device.load(std::memory_order_acquire);
        if (relevant) {
            device_event_pending.store(true, std::memory_order_release);
            wake_device.notify_one();
        }
    }
    return 1;
}

}

bool initialize() {
    std::lock_guard lifecycle(lifecycle_mutex);
    if (status.load() != DeviceStatus::Stopped) return true;
    {
        std::unique_lock lock(mutex);
        output_frequency = twine::audio::output_frequency;
        silent_clock = std::chrono::steady_clock::now();
        silent_frames = {};
        if (!set_frequency_locked(input_frequency, lock)) return false;

    }
    if (!initialize_driver()) {
        const std::string error = hardware_error;
        use_silent_output();
        std::fprintf(stderr, "Hardware audio unavailable (%s); retrying with silent output\n", error.c_str());
    } else {
        status.store(DeviceStatus::Available, std::memory_order_release);

    }
    SDL_AddEventWatch(watch_device, nullptr);
    watching_events = true;
    stop_worker = false;
    device_worker = std::thread([] {
        for (;;) {
            std::unique_lock lock(wake_mutex);
            wake_device.wait_for(lock, std::chrono::milliseconds(100), [] {
                return stop_worker.load() || device_event_pending.load();
            });
            if (stop_worker) return;
            lock.unlock();
            if (state_audio_paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            bool recover;
            {
                std::lock_guard lifecycle(lifecycle_mutex);
                recover = device_event_pending.exchange(false) ||
                    (resume_reopen_pending && std::chrono::steady_clock::now() >= next_resume_reopen);
                std::lock_guard pcm(mutex);
                const auto now = std::chrono::steady_clock::now();
                const auto progress = callback_progress.load(std::memory_order_relaxed);
                if (!device || !playback_started || progress != observed_callback_progress) {
                    observed_callback_progress = progress;
                    last_callback_progress = now;
                } else if (SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_STOPPED ||
                        now - last_callback_progress >= std::chrono::milliseconds(500)) {

                    recover = true;
                }
            }
            if (recover) recover_after_resume();
        }
    });
    return true;
}

void shutdown() {
    stop_worker = true;
    wake_device.notify_one();
    if (device_worker.joinable()) device_worker.join();
    std::lock_guard lifecycle(lifecycle_mutex);
    std::lock_guard lock(mutex);
    if (watching_events) SDL_DelEventWatch(watch_device, nullptr);
    watching_events = false;
    resume_reopen_pending = false;
    device_event_pending = false;
    close_locked();
    output_consumed.notify_all();
    if (owns_audio_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    owns_audio_subsystem = false;
    status.store(DeviceStatus::Stopped, std::memory_order_release);
}

bool recover_after_resume() {
    std::lock_guard lifecycle(lifecycle_mutex);
    if (state_audio_paused.load(std::memory_order_acquire)) {
        device_event_pending = true;
        return false;
    }
    if (status.load() == DeviceStatus::Stopped) return false;
    detach_device();
    if (!initialize_driver()) {
        use_silent_output();
        std::fprintf(
            stderr,
            "TWINE_RESUME audio=reopen_failed error=%s\n",
            SDL_GetError());
        return false;
    }
    resume_reopen_pending = false;
    device_event_pending = false;
    status.store(DeviceStatus::Available, std::memory_order_release);

    return true;
}

DeviceStatus device_status() { return status.load(std::memory_order_acquire); }

DeviceStatus service_device_changes(bool resumed) {
    if (resumed) {
        device_event_pending.store(true, std::memory_order_release);
        wake_device.notify_one();
    }
    return device_status();
}

void queue_samples(int16_t* samples, size_t sample_count) {
    if (samples == nullptr || sample_count < 2) {
        return;
    }

    if (sample_count > static_cast<size_t>(std::numeric_limits<int>::max()) /
            sizeof(int16_t)) {
        std::fprintf(stderr, "Audio buffer is too large: %zu samples\n", sample_count);
        return;
    }

    std::lock_guard producer(producer_mutex);
    std::unique_lock lock(mutex);
    if (stream == nullptr) {
        return;
    }
    advance_silent_device_locked();

    sample_count &= ~size_t{1};

    static std::vector<int16_t> input;
    static std::vector<int16_t> converted;
    input.resize(sample_count);
    const int volume = static_cast<int>(
        std::clamp(recompui::config::sound::get_main_volume(), 0.0, 100.0));
    for (size_t index = 0; index < sample_count; index += 2) {
        input[index] = static_cast<int16_t>(
            static_cast<int32_t>(samples[index + 1]) * volume / 200);
        input[index + 1] = static_cast<int16_t>(
            static_cast<int32_t>(samples[index]) * volume / 200);
    }

    try {
        stream->put(input);
        stream->take(converted);

        if (device && !playback_started && converted.size() >= twine::audio::OutputQueue::max_samples) {
            playback_started = true;
            SDL_PauseAudioDevice(device, 0);
        }
        append_output_locked(converted, lock);
        if (!stream) return;

    } catch (const std::exception& error) {
        std::fprintf(stderr, "Audio output failed: %s\n", error.what());
        ultramodern::quit();
        return;
    }

    const size_t queued_output_frames =
        (output.size() * sizeof(int16_t)) / (sizeof(int16_t) * 2);
    if (device && !playback_started &&
        queued_output_frames >= prebuffer_target_frames) {
        playback_started = true;
        SDL_PauseAudioDevice(device, 0);

    }
}

size_t frames_remaining() {
    std::lock_guard lock(mutex);
    if (stream == nullptr) {
        return 0;
    }
    advance_silent_device_locked();
    const size_t queued_output_frames =
        (output.size() * sizeof(int16_t)) / (sizeof(int16_t) * 2);

    return twine::audio::reportable_input_frames(
        queued_output_frames,
        input_frequency,
        output_frequency);
}

void set_frequency(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    std::lock_guard producer(producer_mutex);
    std::unique_lock lock(mutex);
    advance_silent_device_locked();
    if (frequency != input_frequency && !set_frequency_locked(frequency, lock)) {
        std::fprintf(stderr, "Failed to set audio frequency: %s\n", SDL_GetError());
        ultramodern::quit();
    }
}

}

using namespace twine::audio_host;
namespace {
void write_samples(twine::state::Writer& out, const twine::audio::Samples& samples) {
    if ((samples.size() & 1) || samples.size() > twine::audio::OutputQueue::max_samples)
        throw std::runtime_error("Invalid saved PCM extent");
    out.u32(uint32_t(samples.size()));
    for (size_t i = 0; i < samples.size(); i += 2)
        out.u32(uint32_t(uint16_t(samples[i])) | (uint32_t(uint16_t(samples[i + 1])) << 16));
}
twine::audio::Samples read_samples(twine::state::Reader& in, size_t maximum) {
    const auto count = in.u32();
    if ((count & 1) || count > maximum) throw std::runtime_error("Invalid saved PCM length");
    twine::audio::Samples samples(count);
    for (size_t i = 0; i < count; i += 2) {
        const auto pair = in.u32();
        samples[i] = std::bit_cast<int16_t>(uint16_t(pair));
        samples[i + 1] = std::bit_cast<int16_t>(uint16_t(pair >> 16));
    }
    return samples;
}
}

struct twine::state::AudioPause::Impl {

    std::unique_lock<std::mutex> lifecycle{lifecycle_mutex, std::defer_lock};
    std::unique_lock<std::mutex> lock{mutex, std::defer_lock};
    std::unique_ptr<twine::audio::StateStream> prepared_stream;
    twine::audio::Samples prepared_queue;
    size_t prepared_count = 0;
    std::array<uint8_t, 4096> prepared_dmem{};
    ultramodern::state::AudioState prepared_runtime;
    bool prepared = false, committed = false, acquired = false, reserved = false;
    ~Impl() {
        if (acquired) {
            if (device) {
                if (playback_started) {
                    SDL_PauseAudioDevice(device, 0);
                } else {
                    if (output.size() / 2 >= prebuffer_target_frames) {
                        playback_started = true;
                        SDL_PauseAudioDevice(device, 0);
                    }
                }
            }
            if (!device) silent_clock = std::chrono::steady_clock::now();
            observed_callback_progress = callback_progress.load(std::memory_order_relaxed);
            last_callback_progress = std::chrono::steady_clock::now();
        }
        if (reserved) state_audio_paused.store(false, std::memory_order_release);
    }
};
twine::state::AudioPause::AudioPause() : impl(std::make_unique<Impl>()) {
    bool expected = false;
    if (!state_audio_paused.compare_exchange_strong(expected, true))
        throw std::runtime_error("Audio save-state transaction already active");
    impl->reserved = true;
    impl->lifecycle.lock();
    impl->lock.lock();
    impl->acquired = true;
    if (!stream) throw std::runtime_error("Audio output is unavailable for saving state");
    if (device) SDL_PauseAudioDevice(device, 1);
}
twine::state::AudioPause::~AudioPause() = default;
twine::state::Bytes twine::state::AudioPause::capture() const {
    const char* filter = SDL_GetHint(SDL_HINT_AUDIO_RESAMPLING_MODE);
    if (filter && *filter && std::strcmp(filter, "0") && std::strcmp(filter, "default"))
        throw std::runtime_error("Save states require SDL's default resampler; remove SDL_AUDIO_RESAMPLING_MODE override");
    Writer out; out.u32(1);
    const auto runtime = ultramodern::state::capture_audio();
    ultramodern::state::validate_audio(runtime);
    const auto& saved_stream = stream->captured();
    if (saved_stream.input_rate != runtime.sample_rate || saved_stream.output_rate != 48000)
        throw std::runtime_error("Audio owners disagree at save-state boundary");
    out.fields(runtime.sample_rate, runtime.feedback_offset, saved_stream.input_rate, saved_stream.output_rate);
    write_samples(out, saved_stream.history); write_samples(out, saved_stream.staging);
    write_samples(out, output.capture());
    for (size_t i = 0; i < impl->prepared_dmem.size(); i += 4)
        out.u32(uint32_t(dmem[i]) | (uint32_t(dmem[i + 1]) << 8) |
            (uint32_t(dmem[i + 2]) << 16) | (uint32_t(dmem[i + 3]) << 24));
    return std::move(out.bytes);
}
void twine::state::AudioPause::prepare(std::span<const uint8_t> saved) {
    Reader in(saved);
    if (in.u32() != 1) throw std::runtime_error("Unsupported audio save-state schema");
    ultramodern::state::AudioState runtime;
    twine::audio::StreamState stream;
    in.fields(runtime.sample_rate, runtime.feedback_offset, stream.input_rate, stream.output_rate);
    ultramodern::state::validate_audio(runtime);
    twine::audio::StateStream::validate_rates(stream.input_rate, stream.output_rate);
    if (stream.input_rate != runtime.sample_rate || stream.output_rate != output_frequency)
        throw std::runtime_error("Incompatible save-state audio output format");

    stream.history = read_samples(in, 512 * 24 * 4);
    stream.staging = read_samples(in, 512 * 24 * 2);
    auto queue = read_samples(in, twine::audio::OutputQueue::max_samples);
    std::array<uint8_t, 4096> rsp_memory;
    for (size_t i = 0; i < rsp_memory.size(); i += 4) {
        const auto value = in.u32();
        for (size_t n = 0; n < 4; ++n) rsp_memory[i + n] = uint8_t(value >> (8 * n));
    }
    in.end();
    auto prepared = std::make_unique<twine::audio::StateStream>(stream);
    const auto queue_count = queue.size();
    queue.resize(std::max(size_t(48000 * 2 * 2), queue.size()));
    impl->prepared_stream = std::move(prepared);
    impl->prepared_queue = std::move(queue); impl->prepared_count = queue_count;
    impl->prepared_runtime = runtime; impl->prepared_dmem = rsp_memory;
    impl->prepared = true;
}
void twine::state::AudioPause::commit() noexcept {
    if (!impl->prepared || impl->committed) std::terminate();
    stream.swap(impl->prepared_stream);
    input_frequency = impl->prepared_runtime.sample_rate;
    output.commit(impl->prepared_queue, impl->prepared_count);

    playback_started = impl->prepared_count / 2 >= prebuffer_target_frames;
    std::copy(impl->prepared_dmem.begin(), impl->prepared_dmem.end(), dmem);
    ultramodern::state::restore_audio(impl->prepared_runtime);
    impl->committed = true;
}
