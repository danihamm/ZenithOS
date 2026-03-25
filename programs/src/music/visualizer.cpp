#include "visualizer.hpp"

namespace music_visualizer {

using namespace gui;

static void px_fill(uint32_t* px, int bw, int bh,
                    int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    uint32_t v = c.to_pixel();
    int x0 = gui_clamp(x, 0, bw);
    int y0 = gui_clamp(y, 0, bh);
    int x1 = gui_clamp(x + w, 0, bw);
    int y1 = gui_clamp(y + h, 0, bh);
    for (int row = y0; row < y1; row++) {
        for (int col = x0; col < x1; col++) {
            px[row * bw + col] = v;
        }
    }
}

static void px_hline(uint32_t* px, int bw, int bh,
                     int x, int y, int w, Color c) {
    if (w <= 0 || y < 0 || y >= bh) return;
    uint32_t v = c.to_pixel();
    int x0 = gui_clamp(x, 0, bw);
    int x1 = gui_clamp(x + w, 0, bw);
    for (int col = x0; col < x1; col++) {
        px[y * bw + col] = v;
    }
}

void reset(State& state) {
    for (int i = 0; i < BAR_COUNT; i++) {
        state.levels[i] = 0;
        state.peaks[i] = 0;
        state.peak_hold[i] = 0;
    }
}

void tick(State& state) {
    for (int i = 0; i < BAR_COUNT; i++) {
        if (state.levels[i] > 0) {
            state.levels[i] = state.levels[i] > 3 ? (uint8_t)(state.levels[i] - 3) : 0;
        }
        if (state.peak_hold[i] < 6) {
            state.peak_hold[i]++;
        } else if (state.peaks[i] > 0) {
            state.peaks[i] = state.peaks[i] > 2 ? (uint8_t)(state.peaks[i] - 2) : 0;
        }
    }
}

void feed_pcm(State& state, const int16_t* pcm, int frames, int channels) {
    if (!pcm || frames <= 0 || channels <= 0) return;

    int local_max[BAR_COUNT] = {};
    for (int i = 0; i < frames; i++) {
        int bar = (i * BAR_COUNT) / frames;
        if (bar < 0) bar = 0;
        if (bar >= BAR_COUNT) bar = BAR_COUNT - 1;

        int amp = 0;
        for (int ch = 0; ch < channels; ch++) {
            int sample = pcm[i * channels + ch];
            if (sample < 0) sample = -sample;
            amp += sample;
        }
        amp /= channels;
        if (amp > local_max[bar]) local_max[bar] = amp;
    }

    for (int i = 0; i < BAR_COUNT; i++) {
        int scaled = (local_max[i] * 100) / 32768;
        scaled = (scaled * 3) / 2;
        if (scaled > 100) scaled = 100;

        int blended = scaled;
        if (scaled < state.levels[i]) {
            blended = (state.levels[i] * 3 + scaled) / 4;
        }

        if (blended > state.levels[i]) {
            state.levels[i] = (uint8_t)blended;
        }
        if (state.levels[i] > state.peaks[i]) {
            state.peaks[i] = state.levels[i];
            state.peak_hold[i] = 0;
        }
    }
}

void render(uint32_t* pixels, int bw, int bh, const Rect& rect,
            const State& state, Color accent, Color accent_dark,
            Color track_bg, Color border) {
    if (!pixels || rect.w <= 0 || rect.h <= 0) return;

    px_fill(pixels, bw, bh, rect.x, rect.y, rect.w, rect.h, Color::from_rgb(0xFA, 0xFB, 0xFD));
    px_hline(pixels, bw, bh, rect.x, rect.y, rect.w, border);
    px_hline(pixels, bw, bh, rect.x, rect.y + rect.h - 1, rect.w, border);

    int inner_x = rect.x + 10;
    int inner_y = rect.y + 8;
    int inner_w = rect.w - 20;
    int inner_h = rect.h - 16;
    if (inner_w <= 0 || inner_h <= 0) return;

    int gap = 2;
    int bar_w = (inner_w - gap * (BAR_COUNT - 1)) / BAR_COUNT;
    if (bar_w < 2) bar_w = 2;
    int bars_w = bar_w * BAR_COUNT + gap * (BAR_COUNT - 1);
    int start_x = inner_x + (inner_w - bars_w) / 2;
    int base_y = inner_y + inner_h - 1;
    int usable_h = inner_h - 6;
    if (usable_h < 8) usable_h = 8;

    px_hline(pixels, bw, bh, inner_x, base_y, inner_w, track_bg);

    for (int i = 0; i < BAR_COUNT; i++) {
        int x = start_x + i * (bar_w + gap);
        int bar_h = (state.levels[i] * usable_h) / 100;
        if (bar_h < 2 && state.levels[i] > 0) bar_h = 2;

        if (bar_h > 0) {
            int y = base_y - bar_h;
            int top_h = bar_h / 3;
            if (top_h < 2) top_h = 2;
            if (top_h > bar_h) top_h = bar_h;
            px_fill(pixels, bw, bh, x, y + top_h, bar_w, bar_h - top_h, accent);
            px_fill(pixels, bw, bh, x, y, bar_w, top_h, accent_dark);
        }

        int peak_h = (state.peaks[i] * usable_h) / 100;
        int peak_y = base_y - peak_h;
        if (peak_y < inner_y) peak_y = inner_y;
        px_fill(pixels, bw, bh, x, peak_y, bar_w, 2, border);
    }
}

} // namespace music_visualizer
