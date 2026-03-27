#include "visualizer.hpp"

namespace music_visualizer {

using namespace gui;

static constexpr int SLICE_FRAMES = 256;

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

static Color mix_color(Color a, Color b, int t) {
    t = gui_clamp(t, 0, 255);
    int inv_t = 255 - t;
    return {
        (uint8_t)((a.r * inv_t + b.r * t + 127) / 255),
        (uint8_t)((a.g * inv_t + b.g * t + 127) / 255),
        (uint8_t)((a.b * inv_t + b.b * t + 127) / 255),
        255
    };
}

static int ordered_index(const State& state, int order) {
    if (state.history_count < WAVE_HISTORY) return order;
    int idx = state.write_pos + order;
    if (idx >= WAVE_HISTORY) idx -= WAVE_HISTORY;
    return idx;
}

static void clear_accumulator(State& state) {
    state.accum_frames = 0;
    state.accum_abs_sum = 0;
    state.accum_min = 32767;
    state.accum_max = -32768;
}

static void push_slice(State& state, int min_sample, int max_sample, int avg_abs) {
    if (min_sample > max_sample) return;

    int idx = state.write_pos;
    state.min_samples[idx] = (int16_t)min_sample;
    state.max_samples[idx] = (int16_t)max_sample;

    int scaled_energy = (avg_abs * 100) / 32768;
    scaled_energy = (scaled_energy * 3) / 2;
    if (scaled_energy > 100) scaled_energy = 100;
    state.energy[idx] = (uint8_t)scaled_energy;

    state.write_pos++;
    if (state.write_pos >= WAVE_HISTORY) state.write_pos = 0;
    if (state.history_count < WAVE_HISTORY) state.history_count++;
}

static void sample_history(const State& state, int column, int total_columns,
                           int& min_sample, int& max_sample, int& energy) {
    if (state.history_count <= 0) {
        min_sample = 0;
        max_sample = 0;
        energy = 0;
        return;
    }

    if (state.history_count == 1 || total_columns <= 1) {
        int idx = ordered_index(state, state.history_count - 1);
        min_sample = state.min_samples[idx];
        max_sample = state.max_samples[idx];
        energy = state.energy[idx];
        return;
    }

    int denom = total_columns - 1;
    int max_order = state.history_count - 1;
    int scaled = column * max_order * 256;
    int base = scaled / denom;
    int frac = base & 0xFF;
    int order0 = base >> 8;
    int order1 = order0 < max_order ? order0 + 1 : order0;

    int idx0 = ordered_index(state, order0);
    int idx1 = ordered_index(state, order1);

    int min0 = state.min_samples[idx0];
    int min1 = state.min_samples[idx1];
    int max0 = state.max_samples[idx0];
    int max1 = state.max_samples[idx1];
    int e0 = state.energy[idx0];
    int e1 = state.energy[idx1];

    min_sample = (min0 * (256 - frac) + min1 * frac) / 256;
    max_sample = (max0 * (256 - frac) + max1 * frac) / 256;
    energy = (e0 * (256 - frac) + e1 * frac) / 256;
}

void reset(State& state) {
    for (int i = 0; i < WAVE_HISTORY; i++) {
        state.min_samples[i] = 0;
        state.max_samples[i] = 0;
        state.energy[i] = 0;
    }
    state.write_pos = 0;
    state.history_count = 0;
    clear_accumulator(state);
}

void tick(State& state) {
    (void)state;
}

void feed_pcm(State& state, const int16_t* pcm, int frames, int channels) {
    if (!pcm || frames <= 0 || channels <= 0) return;

    for (int i = 0; i < frames; i++) {
        int mono = 0;
        for (int ch = 0; ch < channels; ch++) {
            mono += pcm[i * channels + ch];
        }
        mono /= channels;

        if (mono < state.accum_min) state.accum_min = mono;
        if (mono > state.accum_max) state.accum_max = mono;

        int abs_mono = mono;
        if (abs_mono < 0) {
            abs_mono = abs_mono == -32768 ? 32768 : -abs_mono;
        }
        state.accum_abs_sum += abs_mono;
        state.accum_frames++;

        if (state.accum_frames >= SLICE_FRAMES) {
            int avg_abs = state.accum_abs_sum / state.accum_frames;
            push_slice(state, state.accum_min, state.accum_max, avg_abs);
            clear_accumulator(state);
        }
    }
}

void render(uint32_t* pixels, int bw, int bh, const Rect& rect,
            const State& state, Color accent, Color accent_dark,
            Color track_bg, Color border) {
    if (!pixels || rect.w <= 0 || rect.h <= 0) return;

    Color panel_bg = Color::from_rgb(0xFA, 0xFB, 0xFD);
    Color guide = mix_color(panel_bg, track_bg, 144);
    Color guide_faint = mix_color(panel_bg, track_bg, 72);
    Color inner_base = mix_color(Color::from_rgb(0xE9, 0xF2, 0xFF), accent, 64);
    Color outer_base = mix_color(track_bg, accent_dark, 72);

    px_fill(pixels, bw, bh, rect.x, rect.y, rect.w, rect.h, panel_bg);
    px_hline(pixels, bw, bh, rect.x, rect.y, rect.w, border);
    px_hline(pixels, bw, bh, rect.x, rect.y + rect.h - 1, rect.w, border);

    int inner_x = rect.x + 10;
    int inner_y = rect.y + 8;
    int inner_w = rect.w - 20;
    int inner_h = rect.h - 16;
    if (inner_w <= 0 || inner_h <= 0) return;

    int center_y = inner_y + inner_h / 2;
    int amplitude = inner_h / 2 - 10;
    if (amplitude < 12) amplitude = 12;

    px_hline(pixels, bw, bh, inner_x, center_y, inner_w, guide);
    px_hline(pixels, bw, bh, inner_x, center_y - amplitude / 2, inner_w, guide_faint);
    px_hline(pixels, bw, bh, inner_x, center_y + amplitude / 2, inner_w, guide_faint);

    if (state.history_count <= 0) return;

    for (int col = 0; col < inner_w; col++) {
        int min_sample, max_sample, energy;
        sample_history(state, col, inner_w, min_sample, max_sample, energy);

        int top = center_y - (max_sample * amplitude) / 32768;
        int bottom = center_y - (min_sample * amplitude) / 32768;
        if (top > bottom) {
            int swap = top;
            top = bottom;
            bottom = swap;
        }

        int pad = 1 + energy / 28;
        top -= pad;
        bottom += pad;

        top = gui_clamp(top, inner_y, inner_y + inner_h - 1);
        bottom = gui_clamp(bottom, inner_y, inner_y + inner_h - 1);
        if (bottom < top) bottom = top;

        int outer_t = 48 + (col * 160) / (inner_w > 1 ? (inner_w - 1) : 1);
        int inner_t = 80 + (col * 175) / (inner_w > 1 ? (inner_w - 1) : 1);
        Color outer = mix_color(outer_base, accent_dark, outer_t);
        Color inner = mix_color(inner_base, accent, inner_t);

        int x = inner_x + col;
        px_fill(pixels, bw, bh, x, top, 1, bottom - top + 1, outer);

        int inner_top = top + 1;
        int inner_bottom = bottom - 1;
        if (inner_bottom >= inner_top) {
            px_fill(pixels, bw, bh, x, inner_top, 1, inner_bottom - inner_top + 1, inner);
        }
    }
}

} // namespace music_visualizer
