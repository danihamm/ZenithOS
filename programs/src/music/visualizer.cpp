#include "visualizer.hpp"

#include <gui/stb_math.h>

namespace music_visualizer {

using namespace gui;

static constexpr int ANALYSIS_HOP = 256;
static constexpr double PI = 3.14159265358979323846;
static constexpr double REFERENCE_MAGNITUDE = (double)ANALYSIS_SIZE * 32767.0 * 0.17;
static constexpr double PROBE_FREQS[BAND_COUNT] = {
    60.0, 76.49, 97.52, 124.33, 158.51, 202.09, 257.64, 328.47,
    418.76, 533.88, 680.64, 867.76, 1106.3, 1410.43, 1798.16, 2292.48,
    2922.68, 3726.13, 4750.46, 6056.37, 7721.28, 9843.88, 12549.98, 16000.0
};
static constexpr double PROBE_SPREADS[PROBE_COUNT] = {0.82, 1.0, 1.22};
static constexpr double PROBE_WEIGHTS[PROBE_COUNT] = {0.65, 1.0, 0.65};

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

static void px_fill_rounded(uint32_t* px, int bw, int bh,
                            int x, int y, int w, int h, int r, Color c) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    int max_r = gui_min(w / 2, h / 2);
    if (r > max_r) r = max_r;

    uint32_t v = c.to_pixel();
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= bh) continue;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= bw) continue;

            bool skip = false;
            int cx = 0, cy = 0;
            if      (col < r      && row < r)      { cx = r - col - 1; cy = r - row - 1; skip = (cx * cx + cy * cy >= r * r); }
            else if (col >= w - r && row < r)      { cx = col - (w - r); cy = r - row - 1; skip = (cx * cx + cy * cy >= r * r); }
            else if (col < r      && row >= h - r) { cx = r - col - 1; cy = row - (h - r); skip = (cx * cx + cy * cy >= r * r); }
            else if (col >= w - r && row >= h - r) { cx = col - (w - r); cy = row - (h - r); skip = (cx * cx + cy * cy >= r * r); }

            if (!skip) px[dy * bw + dx] = v;
        }
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

static double fast_sin(double x) {
    return stb_cos((PI * 0.5) - x);
}

static void prepare_analysis(State& state, int sample_rate) {
    if (sample_rate <= 0 || state.sample_rate == sample_rate) return;

    state.sample_rate = sample_rate;
    state.ring_pos = 0;
    state.ring_fill = 0;
    state.samples_since_analyze = 0;

    for (int i = 0; i < ANALYSIS_SIZE; i++) {
        state.ring[i] = 0;
        state.window[i] = 0.5 - 0.5 * stb_cos((2.0 * PI * (double)i) / (double)(ANALYSIS_SIZE - 1));
    }

    double max_freq = (double)sample_rate * 0.45;
    if (max_freq < 90.0) max_freq = 90.0;

    for (int band = 0; band < BAND_COUNT; band++) {
        for (int probe = 0; probe < PROBE_COUNT; probe++) {
            double freq = PROBE_FREQS[band] * PROBE_SPREADS[probe];
            if (freq > max_freq) freq = max_freq;
            if (freq < 40.0) freq = 40.0;

            double omega = (2.0 * PI * freq) / (double)sample_rate;
            double cosine = stb_cos(omega);
            state.probes[band][probe].cosine = cosine;
            state.probes[band][probe].sine = fast_sin(omega);
            state.probes[band][probe].coeff = 2.0 * cosine;
        }
    }
}

static double run_goertzel(const State& state, const Probe& probe) {
    double q1 = 0.0;
    double q2 = 0.0;
    int pos = state.ring_pos;

    for (int i = 0; i < ANALYSIS_SIZE; i++) {
        double sample = (double)state.ring[pos] * state.window[i];
        double q0 = probe.coeff * q1 - q2 + sample;
        q2 = q1;
        q1 = q0;

        pos++;
        if (pos >= ANALYSIS_SIZE) pos = 0;
    }

    double real = q1 - q2 * probe.cosine;
    double imag = q2 * probe.sine;
    return stb_sqrt(real * real + imag * imag);
}

static void analyze_window(State& state) {
    if (state.ring_fill < ANALYSIS_SIZE || state.sample_rate <= 0) return;

    double raw[BAND_COUNT] = {};

    for (int band = 0; band < BAND_COUNT; band++) {
        double weighted = 0.0;
        double weight_sum = 0.0;

        for (int probe = 0; probe < PROBE_COUNT; probe++) {
            double magnitude = run_goertzel(state, state.probes[band][probe]);
            weighted += magnitude * PROBE_WEIGHTS[probe];
            weight_sum += PROBE_WEIGHTS[probe];
        }

        if (weight_sum > 0.0) weighted /= weight_sum;

        double normalized = weighted / REFERENCE_MAGNITUDE;
        if (normalized < 0.0) normalized = 0.0;

        double display = 100.0 * stb_sqrt(normalized);
        double t = (double)band / (double)(BAND_COUNT - 1);
        double contour = 1.16 + (1.0 - t) * 0.30 + t * t * 0.08;
        display *= contour;

        if (display < 3.0) display = 0.0;
        if (display > 100.0) display = 100.0;
        raw[band] = display;
    }

    for (int band = 0; band < BAND_COUNT; band++) {
        double prev = raw[band > 0 ? band - 1 : band];
        double cur  = raw[band];
        double next = raw[band + 1 < BAND_COUNT ? band + 1 : band];
        double smooth = prev * 0.22 + cur * 0.56 + next * 0.22;
        int target = gui_clamp((int)(smooth + 0.5), 0, 100);

        int current = state.levels[band];
        if (target >= current) current += ((target - current) * 3 + 3) / 4;
        else current -= ((current - target) + 2) / 3;

        current = gui_clamp(current, 0, 100);
        state.levels[band] = (uint8_t)current;

        if (state.levels[band] > state.peaks[band]) {
            state.peaks[band] = state.levels[band];
            state.peak_hold[band] = 0;
        }
    }
}

void reset(State& state) {
    for (int i = 0; i < BAND_COUNT; i++) {
        state.levels[i] = 0;
        state.peaks[i] = 0;
        state.peak_hold[i] = 0;
    }

    state.sample_rate = 0;
    state.ring_pos = 0;
    state.ring_fill = 0;
    state.samples_since_analyze = 0;

    for (int i = 0; i < ANALYSIS_SIZE; i++) {
        state.ring[i] = 0;
        state.window[i] = 0.0;
    }

    for (int band = 0; band < BAND_COUNT; band++) {
        for (int probe = 0; probe < PROBE_COUNT; probe++) {
            state.probes[band][probe].coeff = 0.0;
            state.probes[band][probe].cosine = 0.0;
            state.probes[band][probe].sine = 0.0;
        }
    }
}

void tick(State& state) {
    for (int i = 0; i < BAND_COUNT; i++) {
        if (state.levels[i] > 0)
            state.levels[i] = state.levels[i] > 1 ? (uint8_t)(state.levels[i] - 1) : 0;

        if (state.peak_hold[i] < 10) {
            state.peak_hold[i]++;
        } else if (state.peaks[i] > 0) {
            state.peaks[i] = state.peaks[i] > 1 ? (uint8_t)(state.peaks[i] - 1) : 0;
        }

        if (state.peaks[i] < state.levels[i]) {
            state.peaks[i] = state.levels[i];
            state.peak_hold[i] = 0;
        }
    }
}

void feed_pcm(State& state, const int16_t* pcm, int frames, int channels, int sample_rate) {
    if (!pcm || frames <= 0 || channels <= 0 || sample_rate <= 0) return;

    prepare_analysis(state, sample_rate);

    for (int i = 0; i < frames; i++) {
        int mono = 0;
        for (int ch = 0; ch < channels; ch++) {
            mono += pcm[i * channels + ch];
        }
        mono /= channels;

        state.ring[state.ring_pos] = (int16_t)mono;
        state.ring_pos++;
        if (state.ring_pos >= ANALYSIS_SIZE) state.ring_pos = 0;
        if (state.ring_fill < ANALYSIS_SIZE) state.ring_fill++;

        state.samples_since_analyze++;
        if (state.samples_since_analyze >= ANALYSIS_HOP && state.ring_fill >= ANALYSIS_SIZE) {
            analyze_window(state);
            state.samples_since_analyze = 0;
        }
    }
}

void render(uint32_t* pixels, int bw, int bh, const Rect& rect,
            const State& state, Color accent, Color accent_dark,
            Color track_bg, Color border) {
    if (!pixels || rect.w <= 0 || rect.h <= 0) return;

    Color panel_bg = Color::from_rgb(0xFA, 0xFB, 0xFD);
    Color guide = mix_color(panel_bg, track_bg, 84);
    Color guide_strong = mix_color(panel_bg, track_bg, 114);
    Color shelf = mix_color(panel_bg, track_bg, 126);
    Color accent_light = mix_color(accent, Color::from_rgb(0xFF, 0xFF, 0xFF), 96);
    Color peak_base = mix_color(Color::from_rgb(0xFF, 0xFF, 0xFF), accent, 88);

    px_fill(pixels, bw, bh, rect.x, rect.y, rect.w, rect.h, panel_bg);
    px_hline(pixels, bw, bh, rect.x, rect.y, rect.w, border);
    px_hline(pixels, bw, bh, rect.x, rect.y + rect.h - 1, rect.w, border);

    int inner_x = rect.x + 12;
    int inner_y = rect.y + 10;
    int inner_w = rect.w - 24;
    int inner_h = rect.h - 20;
    if (inner_w <= 0 || inner_h <= 0) return;

    int gap = inner_w > 340 ? 4 : 3;
    int bar_w = (inner_w - gap * (BAND_COUNT - 1)) / BAND_COUNT;
    if (bar_w < 5) {
        gap = 2;
        bar_w = (inner_w - gap * (BAND_COUNT - 1)) / BAND_COUNT;
    }
    if (bar_w < 3) bar_w = 3;

    int total_w = bar_w * BAND_COUNT + gap * (BAND_COUNT - 1);
    int start_x = inner_x + (inner_w - total_w) / 2;
    int track_y = inner_y + 2;
    int track_h = inner_h - 12;
    if (track_h < 18) track_h = inner_h;
    int track_bottom = track_y + track_h;

    px_hline(pixels, bw, bh, inner_x, track_y + track_h / 4, inner_w, guide);
    px_hline(pixels, bw, bh, inner_x, track_y + track_h / 2, inner_w, guide_strong);
    px_hline(pixels, bw, bh, inner_x, track_y + (track_h * 3) / 4, inner_w, guide);
    px_fill_rounded(pixels, bw, bh, inner_x, track_bottom + 4, inner_w, 4, 2, shelf);

    int radius = gui_clamp(bar_w / 2, 2, 5);

    for (int i = 0; i < BAND_COUNT; i++) {
        int x = start_x + i * (bar_w + gap);
        int y = track_y;

        Color capsule_bg = mix_color(panel_bg, track_bg, 96 + ((i & 1) ? 8 : 0));
        px_fill_rounded(pixels, bw, bh, x, y, bar_w, track_h, radius, capsule_bg);

        if (bar_w > 5) {
            Color step_color = mix_color(panel_bg, track_bg, 132);
            for (int step = 1; step < 5; step++) {
                int sy = y + (track_h * step) / 5;
                px_fill(pixels, bw, bh, x + 1, sy, bar_w - 2, 1, step_color);
            }
        }

        int fill_h = (state.levels[i] * (track_h - 2)) / 100;
        if (fill_h < 0) fill_h = 0;
        if (fill_h > 0 && fill_h < 4) fill_h = 4;
        if (fill_h > track_h) fill_h = track_h;

        if (fill_h > 0) {
            int fill_y = y + track_h - fill_h;
            int blend_t = 36 + (i * 160) / (BAND_COUNT - 1);
            Color outer = mix_color(accent_dark, accent, blend_t);
            Color inner = mix_color(outer, accent_light, 104);
            Color gloss = mix_color(inner, Color::from_rgb(0xFF, 0xFF, 0xFF), 92);

            px_fill_rounded(pixels, bw, bh, x, fill_y, bar_w, fill_h, radius, outer);

            if (bar_w > 3 && fill_h > 3) {
                int inner_r = gui_clamp(radius - 1, 1, 4);
                px_fill_rounded(pixels, bw, bh, x + 1, fill_y + 1, bar_w - 2, fill_h - 2, inner_r, inner);
            }

            int gloss_h = fill_h / 4;
            if (gloss_h < 2) gloss_h = 2;
            if (gloss_h > 6) gloss_h = 6;
            if (bar_w > 2 && fill_h > 2) {
                px_fill_rounded(pixels, bw, bh, x + 1, fill_y + 1, bar_w - 2,
                                gloss_h, gui_clamp(radius - 1, 1, 4), gloss);
            }
        }

        int peak_h = (state.peaks[i] * (track_h - 2)) / 100;
        if (peak_h > 0) {
            int peak_y = y + track_h - peak_h - 2;
            if (peak_y < y + 1) peak_y = y + 1;
            Color peak_color = mix_color(peak_base, accent_light, 64 + (i * 72) / (BAND_COUNT - 1));
            int peak_w = bar_w > 4 ? bar_w - 2 : bar_w;
            int peak_x = bar_w > 4 ? x + 1 : x;
            px_fill_rounded(pixels, bw, bh, peak_x, peak_y, peak_w, 3, 1, peak_color);
        }
    }
}

} // namespace music_visualizer
