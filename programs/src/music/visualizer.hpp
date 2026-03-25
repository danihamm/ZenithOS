#pragma once

#include <cstdint>
#include <gui/gui.hpp>

namespace music_visualizer {

static constexpr int BAR_COUNT = 24;

struct State {
    uint8_t levels[BAR_COUNT];
    uint8_t peaks[BAR_COUNT];
    uint8_t peak_hold[BAR_COUNT];
};

void reset(State& state);
void tick(State& state);
void feed_pcm(State& state, const int16_t* pcm, int frames, int channels);
void render(uint32_t* pixels, int bw, int bh, const gui::Rect& rect,
            const State& state, gui::Color accent, gui::Color accent_dark,
            gui::Color track_bg, gui::Color border);

} // namespace music_visualizer
