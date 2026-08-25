#pragma once

#include "n64/common/types.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace n64 {

class AudioInterface;

namespace frontend {

/// SDL2 audio output — pulls stereo s16 LE frames from AudioInterface.
class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    /// Open default SDL audio device at `sample_rate` Hz, stereo s16.
    /// Returns false on failure (emulation continues muted).
    [[nodiscard]] bool init(AudioInterface* ai, int sample_rate = 32000);

    void shutdown();

    /// Drain AI ring into SDL queue (call ~once per UI frame).
    void pump();

    /// If AI DAC rate drifted, close/reopen device (optional, best-effort).
    void sync_rate_from_ai();

    [[nodiscard]] bool active() const noexcept { return dev_ != 0; }
    [[nodiscard]] int sample_rate() const noexcept { return rate_; }
    [[nodiscard]] u64 frames_submitted() const noexcept { return frames_submitted_.load(std::memory_order_relaxed); }

private:
    static void audio_callback(void* userdata, uint8_t* stream, int len);

    AudioInterface* ai_ = nullptr;
    // SDL_AudioDeviceID is Uint32
    u32 dev_ = 0;
    int rate_ = 0;
    bool owns_sdl_audio_ = false;
    std::vector<s16> buf_;
    std::atomic<u64> frames_submitted_{0};
};

} // namespace frontend
} // namespace n64
