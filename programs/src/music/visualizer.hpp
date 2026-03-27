#pragma once

#include <cstdint>
#include <gui/gui.hpp>

namespace music_visualizer {

static constexpr int WAVE_HISTORY = 192;

struct State {
    int16_t min_samples[WAVE_HISTORY];
    int16_t max_samples[WAVE_HISTORY];
    uint8_t energy[WAVE_HISTORY];
    int write_pos;
    int history_count;
    int accum_frames;
    int accum_abs_sum;
    int accum_min;
    int accum_max;
};

void reset(State& state);
void tick(State& state);
void feed_pcm(State& state, const int16_t* pcm, int frames, int channels);
void render(uint32_t* pixels, int bw, int bh, const gui::Rect& rect,
            const State& state, gui::Color accent, gui::Color accent_dark,
            gui::Color track_bg, gui::Color border);

} // namespace music_visualizer
