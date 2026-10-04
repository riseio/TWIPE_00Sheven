#ifndef TWINE_AUDIO_PIPELINE_HPP
#define TWINE_AUDIO_PIPELINE_HPP

#include <cstddef>
#include <cstdint>

namespace twine::audio {

constexpr uint32_t output_frequency = 48000;
constexpr uint32_t vi_rate = 60;

class SilentFrameClock {
    uint64_t remainder = 0;
public:
    uint64_t advance(uint64_t nanoseconds) {
        constexpr uint64_t second = 1'000'000'000;
        const auto fractional = (nanoseconds % second) * output_frequency + remainder;
        remainder = fractional % second;
        return (nanoseconds / second) * output_frequency + fractional / second;
    }
};

inline size_t input_frames_from_output(
    size_t output_frames,
    uint32_t input_frequency,
    uint32_t output_frequency
) {
    if (input_frequency == 0 || output_frequency == 0) {
        return 0;
    }
    const uint64_t frames = output_frames;
    return static_cast<size_t>(
        (frames / output_frequency) * input_frequency +
        ((frames % output_frequency) * input_frequency) /
            output_frequency);
}

inline size_t prebuffer_target(
    uint32_t frequency,
    uint16_t device_buffer_frames
) {
    const uint64_t interval_frames =
        (static_cast<uint64_t>(frequency) + vi_rate - 1) /
        vi_rate;
    const uint64_t device_frames =
        static_cast<uint64_t>(device_buffer_frames) * 2;
    return static_cast<size_t>(
        interval_frames > device_frames ? interval_frames : device_frames);
}

inline size_t reportable_input_frames(
    size_t queued_output_frames,
    uint32_t input_frequency,
    uint32_t output_frequency
) {
    const size_t input_frames = input_frames_from_output(
        queued_output_frames,
        input_frequency,
        output_frequency);
    const size_t scheduling_margin =
        (static_cast<uint64_t>(input_frequency) + vi_rate - 1) / vi_rate;
    return input_frames > scheduling_margin
        ? input_frames - scheduling_margin
        : 0;
}

}

#endif
