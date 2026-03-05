/*
 * fileio.cpp
 * File I/O - MSS (Montauk SpreadSheet) format
 * Copyright (c) 2026 Daniel Hammer
 */

#include "spreadsheet.h"

void save_file() {
    if (g_filepath[0] == '\0') return;

    int count = 0;
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            if (g_cells[r][c].input[0]) count++;

    int size = 4 + 2 + 2;
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            if (g_cells[r][c].input[0])
                size += 1 + 1 + 2 + 1 + str_len(g_cells[r][c].input);

    uint8_t* buf = (uint8_t*)montauk::malloc(size);
    int off = 0;

    buf[off++] = 'M'; buf[off++] = 'S'; buf[off++] = 'S'; buf[off++] = '2';
    buf[off++] = (uint8_t)(count & 0xFF);
    buf[off++] = (uint8_t)((count >> 8) & 0xFF);
    buf[off++] = MAX_COLS;
    buf[off++] = MAX_ROWS;

    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            if (!g_cells[r][c].input[0]) continue;
            buf[off++] = (uint8_t)c;
            buf[off++] = (uint8_t)r;
            int len = str_len(g_cells[r][c].input);
            buf[off++] = (uint8_t)(len & 0xFF);
            buf[off++] = (uint8_t)((len >> 8) & 0xFF);
            uint8_t flags = ((uint8_t)g_cells[r][c].align & 3)
                          | (((uint8_t)g_cells[r][c].fmt & 3) << 2)
                          | (g_cells[r][c].bold ? 0x10 : 0);
            buf[off++] = flags;
            montauk::memcpy(buf + off, g_cells[r][c].input, len);
            off += len;
        }
    }

    int fd = montauk::fcreate(g_filepath);
    if (fd >= 0) {
        montauk::fwrite(fd, buf, 0, off);
        montauk::close(fd);
        g_modified = false;
    }

    montauk::mfree(buf);
}

void load_file(const char* path) {
    int fd = montauk::open(path);
    if (fd < 0) return;

    uint64_t fsize = montauk::getsize(fd);
    if (fsize < 8 || fsize > 1024 * 1024) {
        montauk::close(fd);
        return;
    }

    uint8_t* buf = (uint8_t*)montauk::malloc((int)fsize);
    montauk::read(fd, buf, 0, fsize);
    montauk::close(fd);

    if (buf[0] != 'M' || buf[1] != 'S' || buf[2] != 'S' || (buf[3] != '1' && buf[3] != '2')) {
        montauk::mfree(buf);
        return;
    }
    bool v2 = (buf[3] == '2');

    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) {
            g_cells[r][c].input[0] = '\0';
            g_cells[r][c].display[0] = '\0';
            g_cells[r][c].value = 0;
            g_cells[r][c].type = CT_EMPTY;
            g_cells[r][c].align = ALIGN_AUTO;
            g_cells[r][c].fmt = FMT_AUTO;
            g_cells[r][c].bold = false;
        }

    int off = 4;
    int count = buf[off] | (buf[off + 1] << 8);
    off += 2;
    off += 2; // skip cols/rows

    for (int i = 0; i < count && off < (int)fsize; i++) {
        if (off + 4 > (int)fsize) break;
        int c = buf[off++];
        int r = buf[off++];
        int len = buf[off] | (buf[off + 1] << 8);
        off += 2;

        uint8_t flags = 0;
        if (v2) {
            if (off >= (int)fsize) break;
            flags = buf[off++];
        }

        if (off + len > (int)fsize) break;
        if (c < MAX_COLS && r < MAX_ROWS && len < CELL_TEXT_MAX) {
            montauk::memcpy(g_cells[r][c].input, buf + off, len);
            g_cells[r][c].input[len] = '\0';
            if (v2) {
                g_cells[r][c].align = (CellAlign)(flags & 3);
                g_cells[r][c].fmt = (NumFormat)((flags >> 2) & 3);
                g_cells[r][c].bold = (flags & 0x10) != 0;
            }
        }
        off += len;
    }

    str_cpy(g_filepath, path, 256);
    g_modified = false;
    eval_all_cells();
    montauk::mfree(buf);
}
