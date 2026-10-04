#pragma once

#include <SDL_audio.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>
#include "audio_pipeline.hpp"

namespace twine::audio {
using Samples = std::vector<int16_t>;
struct StreamState {
    uint32_t input_rate = 0, output_rate = 0;
    Samples history, staging;
};

class StateStream {
    struct Delete { void operator()(SDL_AudioStream* p) const { SDL_FreeAudioStream(p); } };
    std::unique_ptr<SDL_AudioStream, Delete> stream;
    StreamState state;
    size_t padding_samples = 0;

    void processed(std::span<const int16_t> samples) {
        const auto capacity = padding_samples * 2;
        const auto keep_new = std::min(samples.size(), capacity);
        const auto keep_old = std::min(state.history.size(), capacity - keep_new);
        std::move(state.history.end() - keep_old, state.history.end(), state.history.begin());
        state.history.resize(keep_old + keep_new);
        std::copy(samples.end() - keep_new, samples.end(), state.history.begin() + keep_old);
    }
    void observe(std::span<const int16_t> samples) {
        if (padding_samples == 0) return;
        while (!samples.empty()) {
            if (state.staging.empty() && samples.size() >= padding_samples) {
                processed(samples); return;
            }
            const auto count = std::min(samples.size(), padding_samples - state.staging.size());
            state.staging.insert(state.staging.end(), samples.begin(), samples.begin() + count);
            samples = samples.subspan(count);
            if (state.staging.size() == padding_samples) {
                processed(state.staging); state.staging.clear();
            }
        }
    }
public:
    static void validate_rates(uint32_t input, uint32_t output) {
        if (input < 8000 || input > 192000 || output < 8000 || output > 192000)
            throw std::runtime_error("Unsupported save-state audio sample rate");
    }
    StateStream(uint32_t input, uint32_t output) {
        validate_rates(input, output);
        state.input_rate = input; state.output_rate = output;
        const size_t padding_frames = input == output ? 0 :
            (input > output ? (uint64_t(512) * input + output - 1) / output : 512);
        padding_samples = padding_frames * 2;
        state.history.reserve(padding_samples * 2);
        state.staging.reserve(padding_samples);
        stream.reset(SDL_NewAudioStream(AUDIO_S16SYS, 2, int(input), AUDIO_S16SYS, 2, int(output)));
        if (!stream) throw std::runtime_error(SDL_GetError());
    }
    explicit StateStream(const StreamState& saved) : StateStream(saved.input_rate, saved.output_rate) {
        if ((saved.history.size() & 1) || (saved.staging.size() & 1) ||
            saved.history.size() > padding_samples * 2 ||
            (!saved.history.empty() && saved.history.size() < padding_samples) ||
            (padding_samples == 0 ? !saved.staging.empty() : saved.staging.size() >= padding_samples))
            throw std::runtime_error("Invalid save-state resampler padding");
        put(saved.history);

        Samples discard; take(discard);
        put(saved.staging);
        if (available() != 0 || state.history != saved.history || state.staging != saved.staging)
            throw std::runtime_error("SDL resampler state reconstruction mismatch");
    }
    const StreamState& captured() const { return state; }
    void put(std::span<const int16_t> samples) {
        if (samples.empty()) return;
        if ((samples.size() & 1) || samples.size() > size_t(INT_MAX) / sizeof(int16_t))
            throw std::runtime_error("Invalid stereo audio input extent");
        if (SDL_AudioStreamPut(stream.get(), samples.data(), int(samples.size_bytes())) != 0)
            throw std::runtime_error(SDL_GetError());
        observe(samples);
    }
    int available() const { return SDL_AudioStreamAvailable(stream.get()); }
    void take(Samples& output) {
        const int bytes = available();
        if (bytes < 0 || (bytes % 4)) throw std::runtime_error("Invalid converted audio extent");
        output.resize(size_t(bytes) / sizeof(int16_t));
        if (bytes && SDL_AudioStreamGet(stream.get(), output.data(), bytes) != bytes)
            throw std::runtime_error(SDL_GetError());
    }

    void finish(Samples& output) {
        take(output);
        const size_t pending = state.staging.size() +
            (state.history.empty() ? 0 : padding_samples);
        if (pending != 0) {
            const size_t frames = (uint64_t(pending / 2) * state.output_rate +
                state.input_rate - 1) / state.input_rate;
            Samples extension(padding_samples * 2, 0), tail;
            if (SDL_AudioStreamPut(stream.get(), extension.data(),
                    int(extension.size() * sizeof(int16_t))) != 0)
                throw std::runtime_error(SDL_GetError());
            take(tail);
            if (tail.size() < frames * 2)
                throw std::runtime_error("SDL resampler failed to drain pending input");
            output.insert(output.end(), tail.begin(), tail.begin() + frames * 2);
        }
        SDL_AudioStreamClear(stream.get());
        state.history.clear();
        state.staging.clear();
    }
};

class OutputQueue {
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::unique_ptr<int16_t[]> ring;
    alignas(64) std::atomic<uint64_t> readPosition{0};
    alignas(64) std::atomic<uint64_t> writePosition{0};
    std::atomic<uint64_t> underrunSamples{0};
public:
    static constexpr size_t backlog_seconds = 2;
    static constexpr size_t max_samples = output_frequency * 2 * backlog_seconds;
    OutputQueue() : ring(std::make_unique_for_overwrite<int16_t[]>(max_samples)) {}
    size_t size() const {
        const auto written = writePosition.load(std::memory_order_relaxed);
        return size_t(written - readPosition.load(std::memory_order_acquire));
    }
    uint64_t underruns() const { return underrunSamples.load(std::memory_order_relaxed); }
    void clear() {
        readPosition.store(0, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_relaxed);
    }
    void append(std::span<const int16_t> samples) {
        const auto written = writePosition.load(std::memory_order_relaxed);
        const auto read = readPosition.load(std::memory_order_acquire);
        const size_t count = size_t(written - read);
        if ((samples.size() & 1) || samples.size() > max_samples - count)
            throw std::runtime_error("Audio output backlog exceeds two seconds at 48 kHz stereo");
        const size_t end = size_t(written % max_samples);
        const size_t part = std::min(samples.size(), max_samples - end);
        std::copy_n(samples.begin(), part, ring.get() + end);
        std::copy(samples.begin() + part, samples.end(), ring.get());
        writePosition.store(written + samples.size(), std::memory_order_release);
    }
    size_t consume(std::span<int16_t> output) noexcept {
        const auto read = readPosition.load(std::memory_order_relaxed);
        const auto written = writePosition.load(std::memory_order_acquire);
        const size_t used = std::min(output.size(), size_t(written - read)) & ~size_t(1);
        const size_t first = size_t(read % max_samples);
        const size_t part = std::min(used, max_samples - first);
        std::copy_n(ring.get() + first, part, output.begin());
        std::copy_n(ring.get(), used - part, output.begin() + part);
        std::fill(output.begin() + used, output.end(), 0);
        if (used != output.size())
            underrunSamples.fetch_add(output.size() - used, std::memory_order_relaxed);
        readPosition.store(read + used, std::memory_order_release);
        return used;
    }

    size_t discard(size_t samples) noexcept {
        const auto read = readPosition.load(std::memory_order_relaxed);
        const auto written = writePosition.load(std::memory_order_acquire);
        const auto used = std::min(samples, size_t(written - read)) & ~size_t(1);
        readPosition.store(read + used, std::memory_order_release);
        return used;
    }
    Samples capture() const {
        const auto read = readPosition.load(std::memory_order_relaxed);
        const size_t count = size_t(writePosition.load(std::memory_order_relaxed) - read);
        Samples saved(count);
        const size_t first = size_t(read % max_samples);
        const size_t part = std::min(count, max_samples - first);
        std::copy_n(ring.get() + first, part, saved.begin());
        std::copy_n(ring.get(), count - part, saved.begin() + part);
        return saved;
    }

    void commit(Samples& prepared, size_t valid_samples) noexcept {
        if (valid_samples > prepared.size() || valid_samples > max_samples || (valid_samples & 1)) std::terminate();
        std::copy_n(prepared.begin(), valid_samples, ring.get());
        readPosition.store(0, std::memory_order_relaxed);
        writePosition.store(valid_samples, std::memory_order_release);
    }
};
}
