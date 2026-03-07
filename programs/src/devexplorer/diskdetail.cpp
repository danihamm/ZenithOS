/*
 * diskdetail.cpp
 * Disk detail popup window
 * Copyright (c) 2026 Daniel Hammer
 */

#include "px.h"

// ============================================================================
// Helpers
// ============================================================================

static void fmt_u64(char* buf, int bufsize, uint64_t v) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[24]; int i = 0;
    while (v > 0) { tmp[i++] = '0' + (int)(v % 10); v /= 10; }
    int j = 0;
    while (i > 0 && j < bufsize - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void dd_table_row(uint32_t* px, int bw, int bh,
                         int x, int y, int col1_w, int row_h,
                         const char* key, const char* value,
                         Color key_col, Color val_col) {
    int fh = font_h();
    int ty = y + (row_h - fh) / 2;
    px_text(px, bw, bh, x + 8, ty, key, key_col);
    px_text(px, bw, bh, x + col1_w + 8, ty, value, val_col);
}

static void dd_feature_row(uint32_t* px, int bw, int bh,
                           int x, int y, int w, int row_h,
                           const char* label, bool supported, const char* extra) {
    int fh = font_h();
    int ty = y + (row_h - fh) / 2;

    int dot_y = y + (row_h - 8) / 2;
    Color dot_col = supported ? Color::from_rgb(0x22, 0x88, 0x22)
                              : Color::from_rgb(0xBB, 0xBB, 0xBB);
    px_fill_rounded(px, bw, bh, x + 10, dot_y, 8, 8, 4, dot_col);

    Color text_col = supported ? TEXT_COLOR : Color::from_rgb(0xAA, 0xAA, 0xAA);
    px_text(px, bw, bh, x + 26, ty, label, text_col);

    if (extra && extra[0]) {
        int ew = text_w(extra);
        px_text(px, bw, bh, x + w - ew - 12, ty, extra, DIM_TEXT);
    }
}

// ============================================================================
// Tab content: General
// ============================================================================

static void dd_draw_general(uint32_t* px, int bw, int bh, DiskDetailState* dd) {
    char line[128];
    int fh = font_h();
    int x = 0;
    int y = DD_TAB_BAR_H + 12;
    int w = bw;
    int col1_w = 110;
    int row_h = fh + 8;
    Color key_col = DIM_TEXT;
    Color val_col = TEXT_COLOR;
    Color hdr_col = ACCENT_COLOR;
    Color border = Color::from_rgb(0xE0, 0xE0, 0xE0);

    // Identification section
    px_text(px, bw, bh, x + 8, y, "Identification", hdr_col);
    y += fh + 8;
    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 1;

    int rowIdx = 0;
    auto table_row = [&](const char* key, const char* val) {
        if (rowIdx++ % 2 == 0)
            px_fill(px, bw, bh, x + 8, y, w - 16, row_h,
                    Color::from_rgb(0xF7, 0xF7, 0xF7));
        dd_table_row(px, bw, bh, x, y, col1_w, row_h, key, val, key_col, val_col);
        y += row_h;
    };

    table_row("Model", dd->info.model);
    table_row("Serial", dd->info.serial);
    table_row("Firmware", dd->info.firmware);

    const char* typeStr = "Unknown";
    if (dd->info.type == 1) typeStr = "SATA";
    else if (dd->info.type == 2) typeStr = "SATAPI";
    table_row("Type", typeStr);

    snprintf(line, sizeof(line), "%d", (int)dd->info.port);
    table_row("AHCI Port", line);

    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 12;

    // Capacity section
    px_text(px, bw, bh, x + 8, y, "Capacity", hdr_col);
    y += fh + 8;
    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 1;

    rowIdx = 0;
    uint64_t totalBytes = dd->info.sectorCount * (uint64_t)dd->info.sectorSizeLog;
    uint64_t totalMB = totalBytes / (1024 * 1024);
    uint64_t totalGB = totalMB / 1024;
    if (totalGB > 0) {
        int fracGB = (int)((totalMB % 1024) * 10 / 1024);
        snprintf(line, sizeof(line), "%d.%d GiB", (int)totalGB, fracGB);
    } else {
        snprintf(line, sizeof(line), "%d MiB", (int)totalMB);
    }
    table_row("Size", line);

    char secbuf[24];
    fmt_u64(secbuf, sizeof(secbuf), dd->info.sectorCount);
    table_row("Sectors", secbuf);

    snprintf(line, sizeof(line), "%d bytes", (int)dd->info.sectorSizeLog);
    table_row("Logical", line);

    snprintf(line, sizeof(line), "%d bytes", (int)dd->info.sectorSizePhys);
    table_row("Physical", line);

    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 12;

    // Interface section
    px_text(px, bw, bh, x + 8, y, "Interface", hdr_col);
    y += fh + 8;
    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 1;

    rowIdx = 0;
    const char* sataSpeed = "Unknown";
    if (dd->info.sataGen == 1) sataSpeed = "SATA I (1.5 Gb/s)";
    else if (dd->info.sataGen == 2) sataSpeed = "SATA II (3.0 Gb/s)";
    else if (dd->info.sataGen == 3) sataSpeed = "SATA III (6.0 Gb/s)";
    table_row("Link Speed", sataSpeed);

    if (dd->info.rpm == 0)
        table_row("Media", "Not reported");
    else if (dd->info.rpm == 1)
        table_row("Media", "Solid State (SSD)");
    else {
        snprintf(line, sizeof(line), "%d RPM", (int)dd->info.rpm);
        table_row("Media", line);
    }

    px_hline(px, bw, bh, x + 8, y, w - 16, border);
}

// ============================================================================
// Tab content: Features
// ============================================================================

static void dd_draw_features(uint32_t* px, int bw, int bh, DiskDetailState* dd) {
    char line[64];
    int fh = font_h();
    int x = 0;
    int y = DD_TAB_BAR_H + 12;
    int w = bw;
    int row_h = fh + 10;
    Color hdr_col = ACCENT_COLOR;
    Color border = Color::from_rgb(0xE0, 0xE0, 0xE0);

    px_text(px, bw, bh, x + 8, y, "Supported Features", hdr_col);
    y += fh + 8;
    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 4;

    dd_feature_row(px, bw, bh, x, y, w, row_h, "48-bit LBA", dd->info.supportsLba48, nullptr);
    y += row_h;

    if (dd->info.supportsNcq) {
        snprintf(line, sizeof(line), "Depth: %d", (int)dd->info.ncqDepth);
        dd_feature_row(px, bw, bh, x, y, w, row_h, "Native Command Queuing (NCQ)", true, line);
    } else {
        dd_feature_row(px, bw, bh, x, y, w, row_h, "Native Command Queuing (NCQ)", false, nullptr);
    }
    y += row_h;

    dd_feature_row(px, bw, bh, x, y, w, row_h, "TRIM (Data Set Management)", dd->info.supportsTrim, nullptr);
    y += row_h;

    dd_feature_row(px, bw, bh, x, y, w, row_h, "S.M.A.R.T.", dd->info.supportsSmart, nullptr);
    y += row_h;

    dd_feature_row(px, bw, bh, x, y, w, row_h, "Write Cache", dd->info.supportsWriteCache, nullptr);
    y += row_h;

    dd_feature_row(px, bw, bh, x, y, w, row_h, "Read Look-Ahead", dd->info.supportsReadAhead, nullptr);
    y += row_h;

    y += 4;
    px_hline(px, bw, bh, x + 8, y, w - 16, border);
    y += 12;

    // Legend
    Color legend_col = Color::from_rgb(0x88, 0x88, 0x88);
    int ly = y + (fh - 8) / 2;
    px_fill_rounded(px, bw, bh, x + 10, ly, 8, 8, 4, Color::from_rgb(0x22, 0x88, 0x22));
    px_text(px, bw, bh, x + 26, y, "Supported", legend_col);

    px_fill_rounded(px, bw, bh, x + 120, ly, 8, 8, 4, Color::from_rgb(0xBB, 0xBB, 0xBB));
    px_text(px, bw, bh, x + 136, y, "Not supported", legend_col);
}

// ============================================================================
// Render
// ============================================================================

void render_disk_detail() {
    auto& dd = g_state.detail;
    if (!dd.open) return;

    uint32_t* px = dd.pixels;
    int bw = dd.win_w, bh = dd.win_h;
    int fh = font_h();

    px_fill(px, bw, bh, 0, 0, bw, bh, BG_COLOR);

    // Tab bar
    px_fill(px, bw, bh, 0, 0, bw, DD_TAB_BAR_H, TOOLBAR_BG);
    px_hline(px, bw, bh, 0, DD_TAB_BAR_H - 1, bw, BORDER_COLOR);

    int tab_w = bw / DD_TAB_COUNT;
    for (int i = 0; i < DD_TAB_COUNT; i++) {
        int tx = i * tab_w;
        bool active = (i == dd.active_tab);

        if (active) {
            px_fill(px, bw, bh, tx, 0, tab_w, DD_TAB_BAR_H, BG_COLOR);
            px_fill(px, bw, bh, tx + 4, DD_TAB_BAR_H - 3, tab_w - 8, 3, ACCENT_COLOR);
        }

        int tw = text_w(dd_tab_labels[i]);
        Color tc = active ? ACCENT_COLOR : DIM_TEXT;
        px_text(px, bw, bh, tx + (tab_w - tw) / 2, (DD_TAB_BAR_H - fh) / 2,
                dd_tab_labels[i], tc);
    }

    // Tab content
    switch (dd.active_tab) {
    case 0: dd_draw_general(px, bw, bh, &dd); break;
    case 1: dd_draw_features(px, bw, bh, &dd); break;
    }
}

// ============================================================================
// Window management
// ============================================================================

void open_disk_detail(int port, const char* model) {
    auto& dd = g_state.detail;

    if (dd.open) close_disk_detail();

    char title[64];
    int i = 0;
    const char* prefix = "Disk: ";
    while (prefix[i]) { title[i] = prefix[i]; i++; }
    int j = 0;
    while (model[j] && i < 62) { title[i++] = model[j++]; }
    title[i] = '\0';

    dd.win_w = DD_INIT_W;
    dd.win_h = DD_INIT_H;
    dd.active_tab = 0;
    montauk::memset(&dd.info, 0, sizeof(dd.info));
    montauk::diskinfo(&dd.info, port);

    Montauk::WinCreateResult wres;
    if (montauk::win_create(title, dd.win_w, dd.win_h, &wres) < 0 || wres.id < 0)
        return;

    dd.win_id = wres.id;
    dd.pixels = (uint32_t*)(uintptr_t)wres.pixelVa;
    dd.open = true;

    render_disk_detail();
    montauk::win_present(dd.win_id);
}

void close_disk_detail() {
    auto& dd = g_state.detail;
    if (!dd.open) return;
    montauk::win_destroy(dd.win_id);
    dd.open = false;
}

bool handle_detail_mouse(int mx, int my, bool clicked) {
    auto& dd = g_state.detail;
    if (!clicked) return false;

    if (my >= 0 && my < DD_TAB_BAR_H) {
        int tab_w = dd.win_w / DD_TAB_COUNT;
        int tab = mx / tab_w;
        if (tab >= 0 && tab < DD_TAB_COUNT)
            dd.active_tab = tab;
        return true;
    }
    return false;
}
