#include "steam_deck_grips.hpp"

#ifdef __linux__
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <string>

namespace {

constexpr uint16_t valve_vendor = 0x28DE;
constexpr uint16_t steam_deck_product = 0x1205;

int event_fd = -1;
bool lower_evdev = false, event_l5 = false, event_r5 = false;
std::array<int, twine::deck_grips::raw_reader_capacity> raw_fds = [] {
    std::array<int, twine::deck_grips::raw_reader_capacity> result{};
    result.fill(-1);
    return result;
}();
std::array<std::string, twine::deck_grips::raw_reader_capacity> raw_paths{};
struct RawHeldState {
    bool a = false;
    bool b = false;
    bool l4 = false;
    bool r4 = false;
    bool l5 = false, r5 = false;
};
std::array<RawHeldState, twine::deck_grips::raw_reader_capacity>
    raw_held_states{};
std::atomic_bool faces_available{false};
auto next_scan = std::chrono::steady_clock::time_point{};
auto next_controller_scan = std::chrono::steady_clock::time_point{};
int32_t deck_controller_instance = -1;
bool unavailable_reported = false;
bool raw_reported = false;

void close_input(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool has_raw_reader() {
    for (int fd : raw_fds) {
        if (fd >= 0) {
            return true;
        }
    }
    return false;
}

void close_raw(size_t index) {
    close_input(raw_fds[index]);
    raw_paths[index].clear();
    raw_held_states[index] = {};
    if (!has_raw_reader()) {
        faces_available.store(false, std::memory_order_release);
        raw_reported = false;
    }
}

void open_deck() {
    const auto now = std::chrono::steady_clock::now();
    if (event_fd >= 0 || has_raw_reader() || now < next_scan) {
        return;
    }
    next_scan = now + std::chrono::seconds(2);
    int last_error = 0;
    if (event_fd < 0) {
        constexpr size_t bits_per_word = sizeof(unsigned long) * 8;
        std::array<unsigned long,
            twine::deck_grips::btn_gripr2 / bits_per_word + 1> key_bits{};
        for (int index = 0; index < 64; ++index) {
            const std::string path = "/dev/input/event" + std::to_string(index);
            const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (errno != ENOENT) {
                    last_error = errno;
                }
                continue;
            }
            input_id id{};
            key_bits.fill(0);
            const bool deck = ioctl(fd, EVIOCGID, &id) == 0 &&
                id.vendor == valve_vendor && id.product == steam_deck_product;
            const bool grips = ioctl(
                fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) >= 0 &&
                (key_bits[twine::deck_grips::btn_gripl / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_gripl % bits_per_word))) != 0 &&
                (key_bits[twine::deck_grips::btn_gripr / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_gripr % bits_per_word))) != 0;
            const bool faces =
                (key_bits[twine::deck_grips::btn_south / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_south % bits_per_word))) != 0 &&
                (key_bits[twine::deck_grips::btn_east / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_east % bits_per_word))) != 0;
            if (deck && grips && faces) {
                event_fd = fd;
                lower_evdev = (key_bits[twine::deck_grips::btn_gripl2 / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_gripl2 % bits_per_word))) &&
                    (key_bits[twine::deck_grips::btn_gripr2 / bits_per_word] &
                    (1UL << (twine::deck_grips::btn_gripr2 % bits_per_word)));
                faces_available.store(true, std::memory_order_release);
                unavailable_reported = false;

                break;
            }
            close(fd);
        }
    }
    if (event_fd < 0 && !has_raw_reader()) {
        size_t raw_count = 0;
        for (int index = 0; index < 32; ++index) {
            const std::string path = "/dev/hidraw" + std::to_string(index);
            const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                if (errno != ENOENT) {
                    last_error = errno;
                }
                continue;
            }
            struct hidraw_devinfo info{};
            if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
                    info.vendor == valve_vendor && info.product == steam_deck_product) {
                raw_fds[raw_count] = fd;
                raw_paths[raw_count] = path;
                ++raw_count;
                unavailable_reported = false;
                if (raw_count == raw_fds.size()) {
                    break;
                }
                continue;
            }
            close(fd);
        }

    }
    if (event_fd < 0 && !has_raw_reader() && !unavailable_reported) {
        std::fprintf(
            stderr,
            "TWINE_CONTROLLER deck_grips=unavailable error=%d\n",
            last_error);
        unavailable_reported = true;
    }
}

}
#endif

twine::deck_grips::Edges twine::deck_grips::poll() {
#ifdef __linux__
    open_deck();
    Edges result{};
    if (event_fd >= 0) {
        input_event event{};
        for (;;) {
            const ssize_t size = read(event_fd, &event, sizeof(event));
            if (size == sizeof(event)) {
                if (event.type == EV_KEY && event.code == btn_gripl2) event_l5 = event.value != 0;
                if (event.type == EV_KEY && event.code == btn_gripr2) event_r5 = event.value != 0;
                const Edges edges = parse_key_event(event.type, event.code, event.value);
                result.a = result.a || edges.a;
                result.b = result.b || edges.b;
                result.l4 = result.l4 || edges.l4;
                result.r4 = result.r4 || edges.r4;
                result.l5 = result.l5 || edges.l5;
                result.r5 = result.r5 || edges.r5;
                continue;
            }
            if (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                std::fprintf(stderr, "TWINE_CONTROLLER deck_grips=evdev_disconnected error=%d\n", errno);
                close_input(event_fd);
                lower_evdev = event_l5 = event_r5 = false;
            }
            break;
        }
    }
    std::array<uint8_t, 64> report{};
    for (size_t index = 0; index < raw_fds.size(); ++index) {
        if (raw_fds[index] < 0) {
            continue;
        }
        for (;;) {
            const ssize_t size = read(
                raw_fds[index], report.data(), report.size());
            if (size > 0) {
                if (is_deck_report(report.data(), static_cast<size_t>(size)) &&
                        !raw_reported) {
                    raw_reported = true;
                    faces_available.store(true, std::memory_order_release);

                }
                RawHeldState &held = raw_held_states[index];
                const Edges edges = parse_report(
                    report.data(),
                    static_cast<size_t>(size),
                    held.a,
                    held.b,
                    held.l4,
                    held.r4, held.l5, held.r5);
                if (edges.lower_available) {
                    result.lower_available = true;
                    result.l5_held |= edges.l5_held; result.r5_held |= edges.r5_held;
                }
                result.a = result.a || edges.a;
                result.b = result.b || edges.b;
                result.l4 = result.l4 || edges.l4;
                result.r4 = result.r4 || edges.r4;
                result.l5 = result.l5 || edges.l5;
                result.r5 = result.r5 || edges.r5;
                continue;
            }
            if (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                std::fprintf(
                    stderr,
                    "TWINE_CONTROLLER deck_grips=disconnected path=%s error=%d\n",
                    raw_paths[index].c_str(),
                    errno);
                close_raw(index);
            }
            break;
        }
    }
    if (event_fd >= 0 && lower_evdev) {
        result.lower_available = true; result.l5_held |= event_l5; result.r5_held |= event_r5;
    }
    for (size_t i = 0; i < raw_fds.size(); ++i) {
        if (raw_fds[i] >= 0 && raw_reported) {
            result.lower_available = true;
            result.l5_held |= raw_held_states[i].l5; result.r5_held |= raw_held_states[i].r5;
        }
    }
    return result;
#else
    return {};
#endif
}

bool twine::deck_grips::physical_faces_available() {
#ifdef __linux__
    return faces_available.load(std::memory_order_acquire);
#else
    return false;
#endif
}

int32_t twine::deck_grips::controller_instance() {
#ifdef __linux__
    if (!physical_faces_available()) {
        deck_controller_instance = -1;
        return -1;
    }
    if (deck_controller_instance >= 0) {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(
            deck_controller_instance);
        if (controller != nullptr &&
                SDL_GameControllerGetAttached(controller) == SDL_TRUE) {
            SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
            if (is_deck_controller(
                    SDL_JoystickGetVendor(joystick),
                    SDL_JoystickGetProduct(joystick))) {
                return deck_controller_instance;
            }
        }
        deck_controller_instance = -1;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < next_controller_scan) {
        return -1;
    }
    next_controller_scan = now + std::chrono::seconds(2);
    const int joystick_count = SDL_NumJoysticks();
    for (int device = 0; device < joystick_count; ++device) {
        if (SDL_IsGameController(device) != SDL_TRUE ||
                !is_deck_controller(
                    SDL_JoystickGetDeviceVendor(device),
                    SDL_JoystickGetDeviceProduct(device))) {
            continue;
        }
        deck_controller_instance = SDL_JoystickGetDeviceInstanceID(device);
        return deck_controller_instance;
    }
    return -1;
#else
    return -1;
#endif
}

void twine::deck_grips::recover_after_resume() {
#ifdef __linux__
    close_input(event_fd);
    lower_evdev = event_l5 = event_r5 = false;
    for (size_t index = 0; index < raw_fds.size(); ++index) {
        close_raw(index);
    }
    raw_held_states = {};
    faces_available.store(false, std::memory_order_release);
    deck_controller_instance = -1;
    next_scan = {};
    next_controller_scan = {};
    unavailable_reported = false;
    raw_reported = false;

#endif
}
