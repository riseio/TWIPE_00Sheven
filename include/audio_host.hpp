#pragma once

#include <cstddef>
#include <memory>
#include "save_state_file.hpp"
#include <cstdint>

namespace twine::audio_host {
enum class DeviceStatus { Stopped, Available, Unavailable };

bool initialize();
void shutdown();
bool recover_after_resume();
DeviceStatus service_device_changes(bool resumed = false);
DeviceStatus device_status();
void queue_samples(int16_t* samples, size_t sample_count);
size_t frames_remaining();
void set_frequency(uint32_t frequency);

}

namespace twine::state {

class AudioPause {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    AudioPause();
    ~AudioPause();
    Bytes capture() const;
    void prepare(std::span<const uint8_t> saved);
    void commit() noexcept;
};
}
