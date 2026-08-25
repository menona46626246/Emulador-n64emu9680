#include "n64/frontend/audio_output.hpp"

#include "n64/ai/ai.hpp"
#include "n64/common/log.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace n64::frontend {

AudioOutput::~AudioOutput() {
    shutdown();
}

bool AudioOutput::init(AudioInterface* ai, int sample_rate) {
    shutdown();
    ai_ = ai;
    if (!ai_) {
        return false;
    }

    if (sample_rate <= 0) {
        sample_rate = static_cast<int>(ai_->sample_rate());
    }
    if (sample_rate <= 0) {
        sample_rate = 32000;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            N64_WARN("SDL_INIT_AUDIO failed: {} — audio muted", SDL_GetError());
            return false;
        }
        owns_sdl_audio_ = true;
    }

    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = reinterpret_cast<SDL_AudioCallback>(audio_callback);
    want.userdata = this;

    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(
        nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev == 0) {
        N64_WARN("SDL_OpenAudioDevice failed: {} — audio muted", SDL_GetError());
        if (owns_sdl_audio_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            owns_sdl_audio_ = false;
        }
        return false;
    }

    dev_ = static_cast<u32>(dev);
    rate_ = have.freq;
    buf_.resize(4096 * 2);
    frames_submitted_.store(0, std::memory_order_relaxed);
    SDL_PauseAudioDevice(static_cast<SDL_AudioDeviceID>(dev_), 0);

    N64_INFO("Audio output opened with dedicated audio callback: {} Hz, {} ch, format=s16 (requested {} Hz)",
             have.freq, have.channels, sample_rate);
    return true;
}

void AudioOutput::shutdown() {
    if (dev_ != 0) {
        SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(dev_));
        dev_ = 0;
    }
    if (owns_sdl_audio_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_sdl_audio_ = false;
    }
    ai_ = nullptr;
    rate_ = 0;
    buf_.clear();
}

void AudioOutput::sync_rate_from_ai() {
    if (!ai_ || dev_ == 0) {
        return;
    }
    const int want = static_cast<int>(ai_->sample_rate());
    if (want <= 0) {
        return;
    }
    // Reopen only on large drift (>2%).
    const int diff = std::abs(want - rate_);
    if (diff * 100 > rate_ * 2) {
        N64_INFO("AI sample rate {} → reopening audio (was {})", want, rate_);
        AudioInterface* ai = ai_;
        shutdown();
        (void)init(ai, want);
    }
}

void AudioOutput::audio_callback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<AudioOutput*>(userdata);
    if (!self || !self->ai_ || len <= 0) {
        std::memset(stream, 0, static_cast<std::size_t>(len));
        return;
    }
    const std::size_t want_frames = static_cast<std::size_t>(len) / (2 * sizeof(s16));
    s16* out_ptr = reinterpret_cast<s16*>(stream);
    const std::size_t got = self->ai_->pull_frames(std::span<s16>(out_ptr, want_frames * 2));
    if (got < want_frames) {
        // Zero-fill on underrun so there's no harsh noise
        std::memset(out_ptr + got * 2, 0, (want_frames - got) * 2 * sizeof(s16));
    }
    self->frames_submitted_.fetch_add(got, std::memory_order_relaxed);
}

void AudioOutput::pump() {
    // Audio is pulled automatically in real-time by SDL's high-priority audio callback thread.
    // The UI thread still owns device reconfiguration when a ROM changes AI_DACRATE.
    sync_rate_from_ai();
}

} // namespace n64::frontend
