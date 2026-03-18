/*
 * audio.h
 * MontaukOS 2D Game Engine - Audio System
 * Wraps MontaukOS audio syscalls for game sound playback
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include <montauk/syscall.h>

namespace engine {

static constexpr int AUDIO_SAMPLE_RATE = 44100;
static constexpr int AUDIO_CHANNELS = 2;
static constexpr int AUDIO_BITS = 16;

struct AudioEngine {
    int handle = -1;
    int volume = 80;

    bool init() {
        handle = montauk::audio_open(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_BITS);
        if (handle < 0) return false;
        montauk::audio_set_volume(handle, volume);
        return true;
    }

    void shutdown() {
        if (handle >= 0) {
            montauk::audio_close(handle);
            handle = -1;
        }
    }

    void set_volume(int percent) {
        volume = percent;
        if (handle >= 0)
            montauk::audio_set_volume(handle, volume);
    }

    // Write raw PCM samples to the audio device.
    // data: signed 16-bit stereo PCM samples
    // size: number of bytes
    bool write_pcm(const void* data, uint32_t size) {
        if (handle < 0) return false;
        return montauk::audio_write(handle, data, size) >= 0;
    }

    // Generate and play a simple tone (square wave) for duration_ms.
    // Useful for basic sound effects. Non-blocking (writes samples to buffer).
    void play_tone(int freq_hz, int duration_ms, int tone_volume = 50) {
        if (handle < 0 || freq_hz <= 0) return;

        int total_samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
        int half_period = AUDIO_SAMPLE_RATE / (2 * freq_hz);
        if (half_period <= 0) half_period = 1;

        int16_t amp = (int16_t)(327 * tone_volume / 100); // ~1% of max
        static constexpr int CHUNK = 512;
        int16_t buf[CHUNK * 2]; // stereo

        int written = 0;
        while (written < total_samples) {
            int n = total_samples - written;
            if (n > CHUNK) n = CHUNK;

            for (int i = 0; i < n; i++) {
                int sample_pos = written + i;
                int16_t val = ((sample_pos / half_period) % 2 == 0) ? amp : -amp;
                buf[i * 2] = val;
                buf[i * 2 + 1] = val;
            }

            montauk::audio_write(handle, buf, n * 4);
            written += n;
        }
    }
};

} // namespace engine
