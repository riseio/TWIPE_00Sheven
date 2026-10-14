#include "steam_deck_motion.hpp"

#ifdef __linux__
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <mutex>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
std::atomic<int> requested{-1};
std::atomic<unsigned> generation{0};
std::atomic<int64_t> last_motion{0};
std::mutex wake_mutex;
std::condition_variable wake;
std::jthread worker;

int64_t milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

void service(std::stop_token stop) {
    int fd = -1, device = -1;
    unsigned epoch = 0;
    twine::deck_motion::ModeLease lease;
    bool reported_error = false;
    const auto read = [&](uint16_t& value) {
        auto report = twine::deck_motion::mode_request(false);
        if (ioctl(fd, HIDIOCSFEATURE(report.size()), report.data()) != int(report.size())) {
            return false;
        }
        report = {};
        const int size = ioctl(fd, HIDIOCGFEATURE(report.size()), report.data());
        return size >= 0 && twine::deck_motion::read_mode(report, size, value);
    };
    const auto write = [&](uint16_t value) {
        auto report = twine::deck_motion::mode_request(true, value);
        return ioctl(fd, HIDIOCSFEATURE(report.size()), report.data()) == int(report.size());
    };
    const auto close_device = [&] {
        if (fd >= 0) {
            if (!lease.release(read, write)) {
                std::fprintf(stderr, "Controller motion settings could not be restored\n");
            }
            close(fd);
        }
        fd = -1;
        device = -1;
        lease = {};
    };
    while (!stop.stop_requested()) {
        const int wanted = requested.load(std::memory_order_acquire);
        const unsigned next_epoch = generation.load(std::memory_order_acquire);
        if (wanted != device || epoch != next_epoch) {
            close_device();
            epoch = next_epoch;
            reported_error = false;
        }
        if (wanted >= 0 && fd < 0) {
            char path[32];
            std::snprintf(path, sizeof(path), "/dev/hidraw%d", wanted);
            fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
            hidraw_devinfo info{};
            if (fd >= 0 && (ioctl(fd, HIDIOCGRAWINFO, &info) != 0 ||
                    info.vendor != 0x28DE || info.product != 0x1205)) {
                close(fd);
                fd = -1;
            }
            device = wanted;
        }
        if (wanted >= 0 && (fd < 0 || milliseconds() - last_motion.load() > 500)) {
            const bool ready = fd >= 0 && lease.enable(read, write);
            if (!ready && !reported_error) {
                std::fprintf(stderr, "Steam Deck motion sensors unavailable; retrying\n");
            }
            reported_error = !ready;
        }
        std::unique_lock lock(wake_mutex);
        wake.wait_for(lock, 2s, [&] {
            return stop.stop_requested() || requested.load() != wanted || generation.load() != epoch;
        });
    }
    close_device();
}
}
#endif

void twine::deck_motion::initialize() {
#ifdef __linux__
    if (!worker.joinable()) { worker = std::jthread(service); }
#endif
}

void twine::deck_motion::shutdown() {
#ifdef __linux__
    worker.request_stop();
    wake.notify_one();
    if (worker.joinable()) { worker.join(); }
#endif
}

void twine::deck_motion::set_source(int index, bool enabled, bool motion_seen) {
#ifdef __linux__
    const int next = enabled && index >= 0 && index < 32 ? index : -1;
    if (next >= 0 && motion_seen) { last_motion.store(milliseconds()); }
    if (requested.exchange(next, std::memory_order_acq_rel) != next) { wake.notify_one(); }
#endif
}

void twine::deck_motion::reset_source() {
#ifdef __linux__
    requested.store(-1, std::memory_order_release);
    last_motion.store(0);
    generation.fetch_add(1, std::memory_order_release);
    wake.notify_one();
#endif
}
