#pragma once

#include <cstdint>
#include <gui/gui.hpp>

namespace music_visualizer {

static constexpr int BAND_COUNT = 24;
static constexpr int PROBE_COUNT = 3;
static constexpr int ANALYSIS_SIZE = 512;

struct Probe {
    double coeff;
    double cosine;
    double sine;
};

struct State {
    uint8_t levels[BAND_COUNT];
    uint8_t peaks[BAND_COUNT];
    uint8_t peak_hold[BAND_COUNT];

    int sample_rate;
    int ring_pos;
    int ring_fill;
    int samples_since_analyze;

    int16_t ring[ANALYSIS_SIZE];
    double window[ANALYSIS_SIZE];
    Probe probes[BAND_COUNT][PROBE_COUNT];
};

void reset(State& state);
void tick(State& state);
void feed_pcm(State& state, const int16_t* pcm, int frames, int channels, int sample_rate);
void render(uint32_t* pixels, int bw, int bh, const gui::Rect& rect,
            const State& state, gui::Color accent, gui::Color accent_dark,
            gui::Color track_bg, gui::Color border);

} // namespace music_visualizer
