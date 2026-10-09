#include "modern_textures.hpp"
#include "local_aot.hpp"
#include "texture_pack.hpp"
#include "recompui/renderer.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "elements/ui_button.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace twine::textures {
namespace {
std::filesystem::path folder;
std::u8string texture_game_id;
std::atomic_bool requested{false}, ready{false}, started{false};
std::atomic_bool installed{false};
std::atomic_bool usable{false};
enum class Operation { Idle, Preparing, Removing };
std::atomic<Operation> operation{Operation::Idle};
std::mutex worker_mutex;
std::jthread worker;
bool artwork_checked = false;
struct ActionLabel {
    recompui::ContextId context = recompui::ContextId::null();
    recompui::ResourceId resource = recompui::ResourceId::null();
};
std::mutex labels_mutex;
std::array<ActionLabel, 1> action_labels;

const char* action_title(bool present) {
    return present ? "Remove Enhanced Textures" : "Generate HD Textures";
}

void register_label(size_t index, recompui::ContextId context, recompui::Element* label) {
    std::lock_guard lock(labels_mutex);
    action_labels[index] = {context, label->resource_id};
    label->set_text(action_title(installed.load()));
}

void publish_installed(bool present) {
    std::array<ActionLabel, 1> labels;
    {
        std::lock_guard lock(labels_mutex);
        if (installed.exchange(present) == present) return;
        labels = action_labels;
    }
    for (auto& label : labels) {
        if (label.context == recompui::ContextId::null()) continue;
        label.context.open();
        label.context.queue_set_text(label.resource, action_title(present));
        label.context.close();
    }
}

bool save_selection(bool enhanced) {
    return recompui::config::graphics::set_enhanced_textures(enhanced);
}

bool original() { return save_selection(false); }

void await_mount(std::future<void> completion) {
    const auto stop = ultramodern::shutdown_token();
    while (completion.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        if (stop.stop_requested())
            throw PreparationCancelled{};
    }
    completion.get();
}

void prepare(bool explicit_request) {
    struct Finish {
        bool queued = false;
        ~Finish() { if (!queued) operation.store(Operation::Idle); }
    } finish;
    try {
        bool verified = false;
        const auto verification = [&verified](uint64_t bytes, uint64_t total) {
            verified = true;
            recompui::open_notification("Enhanced textures",
                "Verifying the existing archive once: " + std::to_string(total ? bytes * 100 / total : 100) +
                "%. Later launches reuse this result unless the archive changes.");
        };
        std::filesystem::path path;
        if (explicit_request) {
            const auto rom = recomp::read_stored_rom(texture_game_id);
            if (rom.empty()) throw std::runtime_error("Load the supported ROM, then try again.");
            if (!local_aot::prepare(rom)) throw PreparationCancelled{};
            path = prepare_pack(rom, folder,
                [](size_t count) {
                    recompui::open_notification(
                        "Preparing enhanced textures",
                        "Enhancing the original artwork: " + std::to_string(count) + " / " +
                            std::to_string(prepared_texture_count) +
                            " textures prepared. This one-time step can take tens of minutes. "
                            "The complete pack is prepared before it is enabled. Later launches reuse it.");

                },
                ultramodern::shutdown_token(), verification);
        } else {
            path = find_prepared_pack(folder, ultramodern::shutdown_token(), verification);
            if (path.empty()) throw std::runtime_error(
                "The installed pack is missing or invalid. Open R4 > Graphics to remove it if present, then select Generate HD Textures.");
        }
        publish_installed(true);
        if (ultramodern::shutdown_token().stop_requested())
            throw PreparationCancelled{};
        if (explicit_request || verified)
            recompui::open_notification("Enhanced textures", "Opening the prepared texture archive...");
        await_mount(recompui::renderer::set_builtin_texture_pack(path));
        ready.store(true);
        usable.store(true);
        recompui::queue_ui_action([explicit_request] {
            operation.store(Operation::Idle);
            recompui::config::graphics::set_texture_pack_available(true);
            recompui::close_prompt();
            if (explicit_request) {
                if (!save_selection(true)) {
                    original();
                    recompui::open_info_prompt("HD textures installed",
                        "The pack was generated, but the texture setting could not be saved. Check that the game data folder is writable.", "OK", [] {});
                    return;
                }
                recompui::open_info_prompt("HD textures ready",
                    "The enhanced textures are installed and enabled. Use L4 or ~ to toggle them.", "OK", [] {});
            }
        });
        finish.queued = true;
    } catch (const PreparationCancelled &) {
        recompui::close_prompt();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "TWINE_TEXTURE_PREPARATION failed=%s\n", error.what());
        usable.store(false);
        publish_installed(has_pack_cache(folder));
        recompui::queue_ui_action([message = std::string(error.what())] {
            operation.store(Operation::Idle);
            recompui::config::graphics::set_texture_pack_available(false);
            recompui::open_info_prompt("Enhanced textures unavailable",
                "The game is using original textures. " + message, "OK", [] {});
        });
        finish.queued = true;
    }
}

void remove_pack() {
    struct Finish {
        bool queued = false;
        ~Finish() { if (!queued) operation.store(Operation::Idle); }
    } finish;
    try {

        await_mount(recompui::renderer::set_builtin_texture_pack({}));
        ready.store(false);
        remove_pack_cache(folder);
        publish_installed(has_pack_cache(folder));
        usable.store(has_prepared_pack(folder));
        recompui::queue_ui_action([] {
            operation.store(Operation::Idle);
            recompui::config::graphics::set_texture_pack_available(usable.load());
            recompui::close_prompt();
        });
        finish.queued = true;
    } catch (const PreparationCancelled &) {
        recompui::close_prompt();
    } catch (const std::exception &error) {
        publish_installed(has_pack_cache(folder));
        usable.store(has_prepared_pack(folder));
        recompui::queue_ui_action([message = std::string(error.what())] {
            operation.store(Operation::Idle);
            recompui::config::graphics::set_texture_pack_available(usable.load());
            recompui::open_info_prompt("Could not remove enhanced textures", message, "OK", [] {});
        });
        finish.queued = true;
    }
}
}

void initialize(const std::filesystem::path &config_path, std::u8string game_id) {
    folder = config_path;
    texture_game_id = std::move(game_id);
    installed.store(has_pack_cache(folder));
    usable.store(has_prepared_pack(folder));
}

bool available() { return usable.load(); }

void configure() {
    recompui::config::graphics::set_texture_pack_available(available());
}

namespace {
void start_preparation(bool explicit_request) {
    std::lock_guard lock(worker_mutex);
    auto expected = Operation::Idle;
    if (!operation.compare_exchange_strong(expected, Operation::Preparing)) return;
    try {
        if (explicit_request) {
            usable.store(false);
            recompui::config::graphics::set_texture_pack_available(false);
        }
        worker = std::jthread([explicit_request] { prepare(explicit_request); });
    } catch (const std::exception& error) {
        operation.store(Operation::Idle);
        recompui::open_info_prompt("Could not prepare HD textures", error.what(), "OK", [] {});
    }
}

void confirm_removal() {
    if (operation.load() != Operation::Idle) return;
    recompui::open_choice_prompt("Remove enhanced textures?",
        "This deletes the generated texture pack and switches to Original textures. "
        "You can generate the pack again later.", "Remove", "Cancel", [] {
            auto expected = Operation::Idle;
            if (!operation.compare_exchange_strong(expected, Operation::Removing)) return;
            if (!original()) {
                operation.store(Operation::Idle);
                recompui::open_info_prompt("Could not save texture setting",
                    "The texture archive was retained because Original mode could not be saved.", "OK", [] {});
                return;
            }
            usable.store(false);
            recompui::config::graphics::set_texture_pack_available(false);
            recompui::open_notification("Enhanced textures", "Removing the stored texture archive...");
            std::lock_guard lock(worker_mutex);
            try {
                worker = std::jthread(remove_pack);
            } catch (const std::exception& error) {
                operation.store(Operation::Idle);
                usable.store(has_prepared_pack(folder));
                configure();
                recompui::open_info_prompt("Could not remove enhanced textures", error.what(), "OK", [] {});
            }
        }, [] {});
}

void manage_pack() {
    if (installed.load()) confirm_removal();
    else generate();
}
}

void generate() {
    if (operation.load() != Operation::Idle) return;
    if (!recomp::is_rom_valid(texture_game_id)) {
        recompui::open_info_prompt("Load a ROM first", "Use Load ROM before generating HD textures.", "OK", [] {});
        return;
    }
    recompui::open_choice_prompt("Generate HD textures?",
        "A valid pack in the game's data folder will be reused. Otherwise the original artwork is enhanced once, "
        "with progress shown throughout. Allow up to 3.8 GB of storage and tens of minutes. "
        "The textures will be enabled when ready.", "Generate / Check", "Cancel",
        [] { start_preparation(true); }, [] {});
}

void select(bool enhanced) {
    enhanced = enhanced && usable.load();
    requested.store(enhanced);
    if (operation.load() == Operation::Removing) {
        if (enhanced)
            original();
        return;
    }

    if (!enhanced || ready.load() || !started.load() || operation.load() != Operation::Idle)
        return;
    start_preparation(false);
}

void load_selected() {
    std::unique_lock lock(worker_mutex);
    if (!artwork_checked) {
        artwork_checked = true;
        const auto artwork = folder / "artwork.rtz";
        try {
            if (std::filesystem::exists(artwork)) {
                if (!std::filesystem::is_regular_file(artwork) ||
                    std::filesystem::file_size(artwork) > 64ULL * 1024 * 1024)
                    throw std::runtime_error("artwork.rtz must be a texture archive smaller than 64 MiB.");

                await_mount(recompui::renderer::set_user_texture_pack(artwork));

            }
        } catch (const PreparationCancelled&) {
            return;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "TWINE_ARTWORK mounted=0 error=%s\n", error.what());
            recompui::open_info_prompt("Custom artwork unavailable",
                "Check artwork.rtz in the game data folder. " + std::string(error.what()), "OK", [] {});
        }
    }
    started.store(true);
    if (!requested.load() || ready.load())
        return;
    auto expected = Operation::Idle;
    if (operation.compare_exchange_strong(expected, Operation::Preparing)) {
        lock.unlock();
        prepare(false);
    } else if (worker.joinable()) {

        worker.join();
    }
}

void shutdown() {

    std::lock_guard lock(worker_mutex);
    if (worker.joinable())
        worker.join();
}

void create_controls(recompui::ContextId context, recompui::Element *parent) {
    class TextureButton : public recompui::Button {
    public:
        TextureButton(recompui::ResourceId id, recompui::Element* parent)
            : Button(id, parent, action_title(installed.load()), recompui::ButtonStyle::Secondary) {
            register_label(0, recompui::get_current_context(), label);
            add_pressed_callback(manage_pack);
            set_margin_top(24.0f);
        }
    };
    context.create_element<TextureButton>(parent);
}
}
