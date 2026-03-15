/*
 * pdf_page.cpp
 * PDF content stream parsing and text extraction
 * Copyright (c) 2026 Daniel Hammer
 */

#include "pdfviewer.h"

// ============================================================================
// Content Stream Tokenizer
// ============================================================================

enum TokType {
    TOK_EOF, TOK_INT, TOK_REAL, TOK_STRING, TOK_HEX_STRING,
    TOK_NAME, TOK_ARRAY_START, TOK_ARRAY_END, TOK_OPERATOR
};

struct Token {
    TokType type;
    float num;
    char str[MAX_TEXT_LEN];
    int str_len;
};

static int next_token(const uint8_t* d, int len, int p, Token* tok) {
    // Skip whitespace
    while (p < len && (d[p] == ' ' || d[p] == '\t' || d[p] == '\n' || d[p] == '\r'))
        p++;

    // Skip comments
    while (p < len && d[p] == '%') {
        while (p < len && d[p] != '\n' && d[p] != '\r') p++;
        while (p < len && (d[p] == ' ' || d[p] == '\t' || d[p] == '\n' || d[p] == '\r'))
            p++;
    }

    if (p >= len) { tok->type = TOK_EOF; return p; }

    // Dictionary << ... >> (e.g., marked-content properties in tagged PDFs)
    if (d[p] == '<' && p + 1 < len && d[p + 1] == '<') {
        p += 2;
        int depth = 1;
        while (p + 1 < len && depth > 0) {
            if (d[p] == '<' && d[p + 1] == '<') { depth++; p += 2; }
            else if (d[p] == '>' && d[p + 1] == '>') { depth--; p += 2; }
            else p++;
        }
        if (p < len && depth > 0) p++; // skip stray last char on malformed input
        tok->type = TOK_NAME;
        tok->str[0] = '\0';
        tok->str_len = 0;
        return p;
    }

    // Stray > or >> (skip gracefully)
    if (d[p] == '>') {
        p++;
        if (p < len && d[p] == '>') p++;
        tok->type = TOK_NAME;
        tok->str[0] = '\0';
        tok->str_len = 0;
        return p;
    }

    // Literal string
    if (d[p] == '(') {
        p++;
        int depth = 1;
        tok->type = TOK_STRING;
        tok->str_len = 0;
        while (p < len && depth > 0) {
            if (d[p] == '\\' && p + 1 < len) {
                p++;
                char c;
                switch (d[p]) {
                    case 'n': c = '\n'; break;
                    case 'r': c = '\r'; break;
                    case 't': c = '\t'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case '(': c = '('; break;
                    case ')': c = ')'; break;
                    case '\\': c = '\\'; break;
                    default:
                        // Octal escape
                        if (d[p] >= '0' && d[p] <= '7') {
                            int val = d[p] - '0';
                            if (p + 1 < len && d[p + 1] >= '0' && d[p + 1] <= '7') {
                                p++; val = val * 8 + (d[p] - '0');
                                if (p + 1 < len && d[p + 1] >= '0' && d[p + 1] <= '7') {
                                    p++; val = val * 8 + (d[p] - '0');
                                }
                            }
                            c = (char)val;
                        } else {
                            c = (char)d[p];
                        }
                        break;
                }
                if (tok->str_len < MAX_TEXT_LEN - 1)
                    tok->str[tok->str_len++] = c;
                p++;
            } else if (d[p] == '(') {
                depth++;
                if (tok->str_len < MAX_TEXT_LEN - 1)
                    tok->str[tok->str_len++] = '(';
                p++;
            } else if (d[p] == ')') {
                depth--;
                if (depth > 0 && tok->str_len < MAX_TEXT_LEN - 1)
                    tok->str[tok->str_len++] = ')';
                p++;
            } else {
                if (tok->str_len < MAX_TEXT_LEN - 1)
                    tok->str[tok->str_len++] = (char)d[p];
                p++;
            }
        }
        tok->str[tok->str_len] = '\0';
        return p;
    }

    // Hex string
    if (d[p] == '<' && (p + 1 >= len || d[p + 1] != '<')) {
        p++;
        tok->type = TOK_HEX_STRING;
        tok->str_len = 0;
        int nibble = -1;
        while (p < len && d[p] != '>') {
            int val = -1;
            if (d[p] >= '0' && d[p] <= '9') val = d[p] - '0';
            else if (d[p] >= 'a' && d[p] <= 'f') val = d[p] - 'a' + 10;
            else if (d[p] >= 'A' && d[p] <= 'F') val = d[p] - 'A' + 10;
            p++;
            if (val < 0) continue;
            if (nibble < 0) {
                nibble = val;
            } else {
                if (tok->str_len < MAX_TEXT_LEN - 1)
                    tok->str[tok->str_len++] = (char)((nibble << 4) | val);
                nibble = -1;
            }
        }
        if (nibble >= 0 && tok->str_len < MAX_TEXT_LEN - 1)
            tok->str[tok->str_len++] = (char)(nibble << 4);
        tok->str[tok->str_len] = '\0';
        if (p < len) p++; // skip '>'
        return p;
    }

    // Array start/end
    if (d[p] == '[') { tok->type = TOK_ARRAY_START; return p + 1; }
    if (d[p] == ']') { tok->type = TOK_ARRAY_END; return p + 1; }

    // Name
    if (d[p] == '/') {
        p++;
        tok->type = TOK_NAME;
        tok->str_len = 0;
        while (p < len && d[p] != ' ' && d[p] != '\t' && d[p] != '\n' &&
               d[p] != '\r' && d[p] != '/' && d[p] != '<' && d[p] != '>' &&
               d[p] != '[' && d[p] != ']' && d[p] != '(' && d[p] != ')') {
            if (tok->str_len < MAX_TEXT_LEN - 1)
                tok->str[tok->str_len++] = (char)d[p];
            p++;
        }
        tok->str[tok->str_len] = '\0';
        return p;
    }

    // Number or operator
    if ((d[p] >= '0' && d[p] <= '9') || d[p] == '-' || d[p] == '+' || d[p] == '.') {
        // Try to parse as number
        float val;
        int np = parse_real_at(d, len, p, &val);
        if (np > p) {
            tok->type = TOK_REAL;
            tok->num = val;
            return np;
        }
    }

    // Operator (keyword)
    tok->type = TOK_OPERATOR;
    tok->str_len = 0;
    while (p < len && d[p] != ' ' && d[p] != '\t' && d[p] != '\n' &&
           d[p] != '\r' && d[p] != '/' && d[p] != '<' && d[p] != '>' &&
           d[p] != '[' && d[p] != ']' && d[p] != '(' && d[p] != ')') {
        if (tok->str_len < MAX_TEXT_LEN - 1)
            tok->str[tok->str_len++] = (char)d[p];
        p++;
    }
    tok->str[tok->str_len] = '\0';
    return p;
}

// ============================================================================
// Text State and Item Output
// ============================================================================

struct TextState {
    float tm[6];    // text matrix [a b c d e f]
    float lm[6];    // text line matrix
    float tl;       // text leading (TL)
    float font_size;
    char font_name[32];
    uint8_t font_flags;
    uint16_t* tounicode; // current font's glyph->Unicode table (may be nullptr)
    TrueTypeFont* embedded_font; // embedded font from PDF, or nullptr
};

// Apply ToUnicode mapping to raw glyph bytes, producing a UTF-8-ish output.
// For BMP codepoints (which is all we handle), just output as Latin-1 where possible.
static int apply_tounicode(const char* raw, int raw_len, char* out, int out_max,
                           const uint16_t* tounicode) {
    int oi = 0;
    for (int i = 0; i < raw_len && oi < out_max - 1; i++) {
        uint8_t glyph = (uint8_t)raw[i];
        uint16_t cp = tounicode ? tounicode[glyph] : (uint16_t)glyph;
        if (cp == 0) cp = glyph; // fallback to raw byte
        if (cp < 128) {
            out[oi++] = (char)cp;
        } else if (cp < 0x800) {
            // 2-byte UTF-8 - but our font renderer likely only handles Latin-1
            // For now, output as Latin-1 if <= 255, otherwise skip
            if (cp <= 255) {
                out[oi++] = (char)cp;
            } else {
                out[oi++] = '?';
            }
        } else {
            out[oi++] = '?';
        }
    }
    out[oi] = '\0';
    return oi;
}

static void add_text_item(PdfPage* page, float x, float y, float size,
                          const char* text, int text_len, uint8_t flags,
                          const uint16_t* tounicode, TrueTypeFont* embedded_font) {
    if (text_len <= 0) return;

    // Apply ToUnicode mapping if available (only when not using embedded font,
    // since embedded subset fonts need the raw character codes for cmap lookup)
    char mapped[MAX_TEXT_LEN];
    if (tounicode && !embedded_font) {
        text_len = apply_tounicode(text, text_len, mapped, MAX_TEXT_LEN, tounicode);
        text = mapped;
    }
    if (text_len <= 0) return;

    // Grow items array if needed
    if (page->item_count >= page->item_cap) {
        int new_cap = page->item_cap ? page->item_cap * 2 : 64;
        TextItem* new_items = (TextItem*)montauk::malloc(new_cap * sizeof(TextItem));
        if (!new_items) return;
        if (page->items) {
            montauk::memcpy(new_items, page->items, page->item_count * sizeof(TextItem));
            montauk::mfree(page->items);
        }
        page->items = new_items;
        page->item_cap = new_cap;
    }

    TextItem* item = &page->items[page->item_count++];
    item->x = x;
    item->y = y;
    item->font_size = size;
    item->flags = flags;
    item->font = embedded_font;

    int copy = text_len < MAX_TEXT_LEN - 1 ? text_len : MAX_TEXT_LEN - 1;
    montauk::memcpy(item->text, text, copy);
    item->text[copy] = '\0';
}

// ============================================================================
// Graphics State
// ============================================================================

static constexpr int MAX_PATH_SEGS  = 512;
static constexpr int MAX_PATH_RECTS = 128;
static constexpr int MAX_GFX_STACK  = 16;

struct PathSeg  { float x1, y1, x2, y2; };
struct PathRect { float x, y, w, h; };

struct GfxState {
    float ctm[6];       // current transformation matrix
    float line_width;
    uint8_t stroke_r, stroke_g, stroke_b;
    uint8_t fill_r, fill_g, fill_b;
};

static void ctm_transform(const float* ctm, float x, float y, float* ox, float* oy) {
    *ox = ctm[0] * x + ctm[2] * y + ctm[4];
    *oy = ctm[1] * x + ctm[3] * y + ctm[5];
}

static float ctm_scale(const float* ctm) {
    // Approximate scale factor (geometric mean of axis scales)
    float sx = ctm[0] * ctm[0] + ctm[1] * ctm[1];
    float sy = ctm[2] * ctm[2] + ctm[3] * ctm[3];
    // sqrt approximation: just use max for simplicity
    if (sx < sy) sx = sy;
    // Manual sqrt via Newton's method (2 iterations)
    if (sx <= 0) return 1.0f;
    float g = sx;
    g = 0.5f * (g + sx / g);
    g = 0.5f * (g + sx / g);
    return g;
}

static void add_gfx_item(PdfPage* page, GfxType type,
                          float x1, float y1, float x2, float y2,
                          float lw, uint8_t r, uint8_t g, uint8_t b) {
    if (page->gfx_count >= page->gfx_cap) {
        int new_cap = page->gfx_cap ? page->gfx_cap * 2 : 64;
        GraphicsItem* ni = (GraphicsItem*)montauk::malloc(new_cap * sizeof(GraphicsItem));
        if (!ni) return;
        if (page->gfx_items) {
            montauk::memcpy(ni, page->gfx_items, page->gfx_count * sizeof(GraphicsItem));
            montauk::mfree(page->gfx_items);
        }
        page->gfx_items = ni;
        page->gfx_cap = new_cap;
    }
    GraphicsItem* item = &page->gfx_items[page->gfx_count++];
    item->type = type;
    item->x1 = x1; item->y1 = y1;
    item->x2 = x2; item->y2 = y2;
    item->line_width = lw;
    item->r = r; item->g = g; item->b = b;
}

static void matrix_multiply(float* result, const float* a, const float* b) {
    // Multiply two 3x3 matrices represented as [a b c d e f]
    // where the matrix is: [a b 0]
    //                       [c d 0]
    //                       [e f 1]
    result[0] = a[0] * b[0] + a[1] * b[2];
    result[1] = a[0] * b[1] + a[1] * b[3];
    result[2] = a[2] * b[0] + a[3] * b[2];
    result[3] = a[2] * b[1] + a[3] * b[3];
    result[4] = a[4] * b[0] + a[5] * b[2] + b[4];
    result[5] = a[4] * b[1] + a[5] * b[3] + b[5];
}

// ============================================================================
// Operand Stack
// ============================================================================

struct Operand {
    float num;
    char str[MAX_TEXT_LEN];
    int str_len;
    bool is_name;
    bool is_string;
};

// ============================================================================
// Content Stream Parser
// ============================================================================

static FontInfo* lookup_font(FontMap* fonts, const char* name) {
    for (int i = 0; i < fonts->count; i++) {
        if (str_len(fonts->fonts[i].name) == str_len(name)) {
            bool match = true;
            for (int j = 0; name[j]; j++) {
                if (fonts->fonts[i].name[j] != name[j]) { match = false; break; }
            }
            if (match) return &fonts->fonts[i];
        }
    }
    return nullptr;
}

void parse_page(int page_idx, int page_obj_num) {
    PdfPage* page = &g_doc.pages[page_idx];

    // Build font map for this page
    FontMap* fonts = (FontMap*)montauk::malloc(sizeof(FontMap));
    if (!fonts) return;
    build_font_map(page_obj_num, fonts);

    // Get content stream(s)
    int start, end;
    if (find_obj_content(page_obj_num, &start, &end) < 0) {
        for (int fi = 0; fi < fonts->count; fi++)
        if (fonts->fonts[fi].tounicode) montauk::mfree(fonts->fonts[fi].tounicode);
    montauk::mfree(fonts);
        return;
    }

    const uint8_t* d = g_doc.data;
    int len = g_doc.data_len;

    int contents_pos = dict_lookup(d, len, start, "Contents");
    if (contents_pos < 0) {
        for (int fi = 0; fi < fonts->count; fi++)
        if (fonts->fonts[fi].tounicode) montauk::mfree(fonts->fonts[fi].tounicode);
    montauk::mfree(fonts);
        return;
    }

    // Collect content stream object numbers
    int content_objs[32];
    int content_count = 0;

    int cp = skip_ws(d, len, contents_pos);
    if (cp < len && d[cp] == '[') {
        // Array of references
        cp++;
        while (cp < len && d[cp] != ']' && content_count < 32) {
            cp = skip_ws(d, len, cp);
            if (cp >= len || d[cp] == ']') break;
            int ref_num;
            int rp = parse_ref_at(d, len, cp, &ref_num);
            if (rp > 0) {
                content_objs[content_count++] = ref_num;
                cp = rp;
            } else {
                cp++;
            }
        }
    } else {
        // Single reference
        int ref_num;
        if (parse_ref_at(d, len, cp, &ref_num) > 0) {
            content_objs[content_count++] = ref_num;
        }
    }

    // Concatenate and parse all content streams
    for (int ci = 0; ci < content_count; ci++) {
        int stream_len;
        uint8_t* stream_data = get_stream_data(content_objs[ci], &stream_len);
        if (!stream_data) continue;

        // Parse the content stream
        Operand* ops = (Operand*)montauk::malloc(MAX_OPERANDS * sizeof(Operand));
        if (!ops) { montauk::mfree(stream_data); continue; }
        int op_count = 0;

        TextState ts;
        ts.tm[0] = 1; ts.tm[1] = 0; ts.tm[2] = 0; ts.tm[3] = 1; ts.tm[4] = 0; ts.tm[5] = 0;
        ts.lm[0] = 1; ts.lm[1] = 0; ts.lm[2] = 0; ts.lm[3] = 1; ts.lm[4] = 0; ts.lm[5] = 0;
        ts.tl = 0;
        ts.font_size = 12;
        ts.font_name[0] = '\0';
        ts.font_flags = 0;
        ts.tounicode = nullptr;
        ts.embedded_font = nullptr;

        // Graphics state
        GfxState gs;
        gs.ctm[0] = 1; gs.ctm[1] = 0; gs.ctm[2] = 0; gs.ctm[3] = 1;
        gs.ctm[4] = 0; gs.ctm[5] = 0;
        gs.line_width = 1.0f;
        gs.stroke_r = 0; gs.stroke_g = 0; gs.stroke_b = 0;
        gs.fill_r = 0; gs.fill_g = 0; gs.fill_b = 0;

        GfxState gs_stack[MAX_GFX_STACK];
        int gs_depth = 0;

        // Path accumulation
        PathSeg* path_segs = (PathSeg*)montauk::malloc(MAX_PATH_SEGS * sizeof(PathSeg));
        PathRect* path_rects = (PathRect*)montauk::malloc(MAX_PATH_RECTS * sizeof(PathRect));
        int seg_count = 0, rect_count = 0;
        float path_cx = 0, path_cy = 0; // current point
        float path_sx = 0, path_sy = 0; // subpath start

        bool in_text = false;
        Token tok;
        int pos = 0;

        while (pos < stream_len) {
            pos = next_token(stream_data, stream_len, pos, &tok);
            if (tok.type == TOK_EOF) break;

            // Accumulate operands
            if (tok.type == TOK_REAL || tok.type == TOK_INT) {
                if (op_count < MAX_OPERANDS) {
                    ops[op_count].num = tok.num;
                    ops[op_count].is_name = false;
                    ops[op_count].is_string = false;
                    ops[op_count].str_len = 0;
                    op_count++;
                }
                continue;
            }
            if (tok.type == TOK_NAME) {
                if (op_count < MAX_OPERANDS) {
                    ops[op_count].is_name = true;
                    ops[op_count].is_string = false;
                    str_cpy(ops[op_count].str, tok.str, MAX_TEXT_LEN);
                    ops[op_count].str_len = tok.str_len;
                    op_count++;
                }
                continue;
            }
            if (tok.type == TOK_STRING || tok.type == TOK_HEX_STRING) {
                if (op_count < MAX_OPERANDS) {
                    ops[op_count].is_name = false;
                    ops[op_count].is_string = true;
                    montauk::memcpy(ops[op_count].str, tok.str, tok.str_len);
                    ops[op_count].str[tok.str_len] = '\0';
                    ops[op_count].str_len = tok.str_len;
                    ops[op_count].num = 0;
                    op_count++;
                }
                continue;
            }
            if (tok.type == TOK_ARRAY_START) {
                // Special handling for TJ arrays
                // Collect items until ']'
                // Build concatenated string from string elements
                char tj_buf[MAX_TEXT_LEN];
                int tj_len = 0;

                while (pos < stream_len) {
                    Token atk;
                    int next_pos = next_token(stream_data, stream_len, pos, &atk);
                    if (atk.type == TOK_EOF) break;
                    if (atk.type == TOK_ARRAY_END) { pos = next_pos; break; }

                    if ((atk.type == TOK_STRING || atk.type == TOK_HEX_STRING) &&
                        tj_len + atk.str_len < MAX_TEXT_LEN - 1) {
                        montauk::memcpy(tj_buf + tj_len, atk.str, atk.str_len);
                        tj_len += atk.str_len;
                    }
                    // Number entries in TJ arrays are kerning values - skip them
                    pos = next_pos;
                }
                tj_buf[tj_len] = '\0';

                if (tj_len > 0 && op_count < MAX_OPERANDS) {
                    ops[op_count].is_name = false;
                    ops[op_count].is_string = true;
                    montauk::memcpy(ops[op_count].str, tj_buf, tj_len + 1);
                    ops[op_count].str_len = tj_len;
                    ops[op_count].num = 0;
                    op_count++;
                }
                continue;
            }

            // Process operator
            if (tok.type != TOK_OPERATOR) { op_count = 0; continue; }

            const char* op = tok.str;

            // BT - begin text
            if (op[0] == 'B' && op[1] == 'T' && op[2] == '\0') {
                in_text = true;
                ts.tm[0] = 1; ts.tm[1] = 0; ts.tm[2] = 0; ts.tm[3] = 1;
                ts.tm[4] = 0; ts.tm[5] = 0;
                ts.lm[0] = 1; ts.lm[1] = 0; ts.lm[2] = 0; ts.lm[3] = 1;
                ts.lm[4] = 0; ts.lm[5] = 0;
            }
            // ET - end text
            else if (op[0] == 'E' && op[1] == 'T' && op[2] == '\0') {
                in_text = false;
            }
            // Tf - set font
            else if (op[0] == 'T' && op[1] == 'f' && op[2] == '\0' && in_text) {
                if (op_count >= 2 && ops[0].is_name) {
                    str_cpy(ts.font_name, ops[0].str, 32);
                    ts.font_size = ops[1].num;
                    FontInfo* fi = lookup_font(fonts, ts.font_name);
                    ts.font_flags = fi ? fi->flags : 0;
                    ts.tounicode = fi ? fi->tounicode : nullptr;
                    ts.embedded_font = fi ? fi->embedded_font : nullptr;
                }
            }
            // TL - set text leading
            else if (op[0] == 'T' && op[1] == 'L' && op[2] == '\0' && in_text) {
                if (op_count >= 1) ts.tl = ops[0].num;
            }
            // Td - move text position
            else if (op[0] == 'T' && op[1] == 'd' && op[2] == '\0' && in_text) {
                if (op_count >= 2) {
                    float tx = ops[0].num;
                    float ty = ops[1].num;
                    float translate[6] = {1, 0, 0, 1, tx, ty};
                    float result[6];
                    matrix_multiply(result, translate, ts.lm);
                    for (int i = 0; i < 6; i++) { ts.tm[i] = result[i]; ts.lm[i] = result[i]; }
                }
            }
            // TD - move text position and set leading
            else if (op[0] == 'T' && op[1] == 'D' && op[2] == '\0' && in_text) {
                if (op_count >= 2) {
                    ts.tl = -ops[1].num;
                    float tx = ops[0].num;
                    float ty = ops[1].num;
                    float translate[6] = {1, 0, 0, 1, tx, ty};
                    float result[6];
                    matrix_multiply(result, translate, ts.lm);
                    for (int i = 0; i < 6; i++) { ts.tm[i] = result[i]; ts.lm[i] = result[i]; }
                }
            }
            // Tm - set text matrix
            else if (op[0] == 'T' && op[1] == 'm' && op[2] == '\0' && in_text) {
                if (op_count >= 6) {
                    for (int i = 0; i < 6; i++) {
                        ts.tm[i] = ops[i].num;
                        ts.lm[i] = ops[i].num;
                    }
                }
            }
            // T* - move to start of next line
            else if (op[0] == 'T' && op[1] == '*' && op[2] == '\0' && in_text) {
                float translate[6] = {1, 0, 0, 1, 0, -ts.tl};
                float result[6];
                matrix_multiply(result, translate, ts.lm);
                for (int i = 0; i < 6; i++) { ts.tm[i] = result[i]; ts.lm[i] = result[i]; }
            }
            // Tj - show string
            else if (op[0] == 'T' && op[1] == 'j' && op[2] == '\0' && in_text) {
                if (op_count >= 1 && ops[0].is_string && ops[0].str_len > 0) {
                    float eff_size = ts.font_size;
                    // Scale by text matrix
                    float sy = ts.tm[3];
                    if (sy < 0) sy = -sy;
                    if (sy > 0.01f) eff_size *= sy;

                    float gx, gy;
                    ctm_transform(gs.ctm, ts.tm[4], ts.tm[5], &gx, &gy);
                    add_text_item(page, gx, gy, eff_size,
                                  ops[0].str, ops[0].str_len, ts.font_flags, ts.tounicode, ts.embedded_font);

                    // Advance text position using font metrics when available
                    TrueTypeFont* adv_font = ts.embedded_font;
                    if (!adv_font) {
                        adv_font = g_font;
                        if ((ts.font_flags & 1) && g_font_bold) adv_font = g_font_bold;
                        if ((ts.font_flags & 4) && g_font_mono) adv_font = g_font_mono;
                    }
                    float advance;
                    if (adv_font) {
                        int msz = (int)(ts.font_size + 0.5f);
                        if (msz < 4) msz = 4;
                        if (msz > 120) msz = 120;
                        advance = (float)adv_font->measure_text(ops[0].str, msz);
                    } else {
                        advance = ops[0].str_len * ts.font_size * 0.5f;
                    }
                    ts.tm[4] += advance * ts.tm[0];
                    ts.tm[5] += advance * ts.tm[1];
                }
            }
            // TJ - show string (array already concatenated)
            else if (op[0] == 'T' && op[1] == 'J' && op[2] == '\0' && in_text) {
                if (op_count >= 1 && ops[op_count - 1].is_string && ops[op_count - 1].str_len > 0) {
                    Operand* sop = &ops[op_count - 1];
                    float eff_size = ts.font_size;
                    float sy = ts.tm[3];
                    if (sy < 0) sy = -sy;
                    if (sy > 0.01f) eff_size *= sy;

                    float gx, gy;
                    ctm_transform(gs.ctm, ts.tm[4], ts.tm[5], &gx, &gy);
                    add_text_item(page, gx, gy, eff_size,
                                  sop->str, sop->str_len, ts.font_flags, ts.tounicode, ts.embedded_font);

                    TrueTypeFont* adv_font = ts.embedded_font;
                    if (!adv_font) {
                        adv_font = g_font;
                        if ((ts.font_flags & 1) && g_font_bold) adv_font = g_font_bold;
                        if ((ts.font_flags & 4) && g_font_mono) adv_font = g_font_mono;
                    }
                    float advance;
                    if (adv_font) {
                        int msz = (int)(ts.font_size + 0.5f);
                        if (msz < 4) msz = 4;
                        if (msz > 120) msz = 120;
                        advance = (float)adv_font->measure_text(sop->str, msz);
                    } else {
                        advance = sop->str_len * ts.font_size * 0.5f;
                    }
                    ts.tm[4] += advance * ts.tm[0];
                    ts.tm[5] += advance * ts.tm[1];
                }
            }
            // ' (single quote) - move to next line and show string
            else if (op[0] == '\'' && op[1] == '\0' && in_text) {
                // T*
                float translate[6] = {1, 0, 0, 1, 0, -ts.tl};
                float result[6];
                matrix_multiply(result, translate, ts.lm);
                for (int i = 0; i < 6; i++) { ts.tm[i] = result[i]; ts.lm[i] = result[i]; }

                // Tj
                if (op_count >= 1 && ops[0].is_string && ops[0].str_len > 0) {
                    float eff_size = ts.font_size;
                    float sy = ts.tm[3];
                    if (sy < 0) sy = -sy;
                    if (sy > 0.01f) eff_size *= sy;

                    float gx, gy;
                    ctm_transform(gs.ctm, ts.tm[4], ts.tm[5], &gx, &gy);
                    add_text_item(page, gx, gy, eff_size,
                                  ops[0].str, ops[0].str_len, ts.font_flags, ts.tounicode, ts.embedded_font);
                }
            }
            // " (double quote) - set aw, ac, move to next line, show string
            else if (op[0] == '"' && op[1] == '\0' && in_text) {
                // T*
                float translate[6] = {1, 0, 0, 1, 0, -ts.tl};
                float result[6];
                matrix_multiply(result, translate, ts.lm);
                for (int i = 0; i < 6; i++) { ts.tm[i] = result[i]; ts.lm[i] = result[i]; }

                // Show string (third operand)
                if (op_count >= 3 && ops[2].is_string && ops[2].str_len > 0) {
                    float eff_size = ts.font_size;
                    float sy = ts.tm[3];
                    if (sy < 0) sy = -sy;
                    if (sy > 0.01f) eff_size *= sy;

                    float gx, gy;
                    ctm_transform(gs.ctm, ts.tm[4], ts.tm[5], &gx, &gy);
                    add_text_item(page, gx, gy, eff_size,
                                  ops[2].str, ops[2].str_len, ts.font_flags, ts.tounicode, ts.embedded_font);
                }
            }

            // ---- Graphics state operators ----
            // q - save graphics state
            else if (op[0] == 'q' && op[1] == '\0') {
                if (gs_depth < MAX_GFX_STACK)
                    gs_stack[gs_depth++] = gs;
            }
            // Q - restore graphics state
            else if (op[0] == 'Q' && op[1] == '\0') {
                if (gs_depth > 0)
                    gs = gs_stack[--gs_depth];
            }
            // cm - concat matrix
            else if (op[0] == 'c' && op[1] == 'm' && op[2] == '\0') {
                if (op_count >= 6) {
                    float m[6] = { ops[0].num, ops[1].num, ops[2].num,
                                   ops[3].num, ops[4].num, ops[5].num };
                    float r[6];
                    // new_ctm = m * old_ctm
                    r[0] = m[0]*gs.ctm[0] + m[1]*gs.ctm[2];
                    r[1] = m[0]*gs.ctm[1] + m[1]*gs.ctm[3];
                    r[2] = m[2]*gs.ctm[0] + m[3]*gs.ctm[2];
                    r[3] = m[2]*gs.ctm[1] + m[3]*gs.ctm[3];
                    r[4] = m[4]*gs.ctm[0] + m[5]*gs.ctm[2] + gs.ctm[4];
                    r[5] = m[4]*gs.ctm[1] + m[5]*gs.ctm[3] + gs.ctm[5];
                    for (int i = 0; i < 6; i++) gs.ctm[i] = r[i];
                }
            }
            // w - set line width
            else if (op[0] == 'w' && op[1] == '\0' && !in_text) {
                if (op_count >= 1) gs.line_width = ops[0].num;
            }
            // ---- Color operators ----
            // g - set fill gray
            else if (op[0] == 'g' && op[1] == '\0') {
                if (op_count >= 1) {
                    uint8_t v = (uint8_t)(ops[0].num * 255);
                    gs.fill_r = v; gs.fill_g = v; gs.fill_b = v;
                }
            }
            // G - set stroke gray
            else if (op[0] == 'G' && op[1] == '\0') {
                if (op_count >= 1) {
                    uint8_t v = (uint8_t)(ops[0].num * 255);
                    gs.stroke_r = v; gs.stroke_g = v; gs.stroke_b = v;
                }
            }
            // rg - set fill RGB
            else if (op[0] == 'r' && op[1] == 'g' && op[2] == '\0') {
                if (op_count >= 3) {
                    gs.fill_r = (uint8_t)(ops[0].num * 255);
                    gs.fill_g = (uint8_t)(ops[1].num * 255);
                    gs.fill_b = (uint8_t)(ops[2].num * 255);
                }
            }
            // RG - set stroke RGB
            else if (op[0] == 'R' && op[1] == 'G' && op[2] == '\0') {
                if (op_count >= 3) {
                    gs.stroke_r = (uint8_t)(ops[0].num * 255);
                    gs.stroke_g = (uint8_t)(ops[1].num * 255);
                    gs.stroke_b = (uint8_t)(ops[2].num * 255);
                }
            }
            // k - set fill CMYK (approximate as RGB)
            else if (op[0] == 'k' && op[1] == '\0') {
                if (op_count >= 4) {
                    float c_ = ops[0].num, m_ = ops[1].num, y_ = ops[2].num, k_ = ops[3].num;
                    gs.fill_r = (uint8_t)((1 - c_) * (1 - k_) * 255);
                    gs.fill_g = (uint8_t)((1 - m_) * (1 - k_) * 255);
                    gs.fill_b = (uint8_t)((1 - y_) * (1 - k_) * 255);
                }
            }
            // K - set stroke CMYK
            else if (op[0] == 'K' && op[1] == '\0') {
                if (op_count >= 4) {
                    float c_ = ops[0].num, m_ = ops[1].num, y_ = ops[2].num, k_ = ops[3].num;
                    gs.stroke_r = (uint8_t)((1 - c_) * (1 - k_) * 255);
                    gs.stroke_g = (uint8_t)((1 - m_) * (1 - k_) * 255);
                    gs.stroke_b = (uint8_t)((1 - y_) * (1 - k_) * 255);
                }
            }
            // ---- Path construction operators ----
            // m - moveto
            else if (op[0] == 'm' && op[1] == '\0' && !in_text) {
                if (op_count >= 2) {
                    ctm_transform(gs.ctm, ops[0].num, ops[1].num, &path_cx, &path_cy);
                    path_sx = path_cx;
                    path_sy = path_cy;
                }
            }
            // l - lineto
            else if (op[0] == 'l' && op[1] == '\0' && !in_text) {
                if (op_count >= 2 && seg_count < MAX_PATH_SEGS) {
                    float nx, ny;
                    ctm_transform(gs.ctm, ops[0].num, ops[1].num, &nx, &ny);
                    path_segs[seg_count].x1 = path_cx;
                    path_segs[seg_count].y1 = path_cy;
                    path_segs[seg_count].x2 = nx;
                    path_segs[seg_count].y2 = ny;
                    seg_count++;
                    path_cx = nx;
                    path_cy = ny;
                }
            }
            // re - rectangle (x y w h)
            else if (op[0] == 'r' && op[1] == 'e' && op[2] == '\0') {
                if (op_count >= 4 && rect_count < MAX_PATH_RECTS) {
                    float rx = ops[0].num, ry = ops[1].num;
                    float rw = ops[2].num, rh = ops[3].num;
                    // Transform corners through CTM
                    float tx, ty;
                    ctm_transform(gs.ctm, rx, ry, &tx, &ty);
                    float tw = rw * ctm_scale(gs.ctm);
                    float th = rh * ctm_scale(gs.ctm);
                    path_rects[rect_count].x = tx;
                    path_rects[rect_count].y = ty;
                    path_rects[rect_count].w = tw;
                    path_rects[rect_count].h = th;
                    rect_count++;
                    // re also adds 4 line segments and sets current point
                    if (seg_count + 4 <= MAX_PATH_SEGS) {
                        float x0, y0, x1, y1, x2, y2, x3, y3;
                        ctm_transform(gs.ctm, rx, ry, &x0, &y0);
                        ctm_transform(gs.ctm, rx + rw, ry, &x1, &y1);
                        ctm_transform(gs.ctm, rx + rw, ry + rh, &x2, &y2);
                        ctm_transform(gs.ctm, rx, ry + rh, &x3, &y3);
                        path_segs[seg_count++] = {x0, y0, x1, y1};
                        path_segs[seg_count++] = {x1, y1, x2, y2};
                        path_segs[seg_count++] = {x2, y2, x3, y3};
                        path_segs[seg_count++] = {x3, y3, x0, y0};
                    }
                    path_cx = tx;
                    path_cy = ty;
                    path_sx = tx;
                    path_sy = ty;
                }
            }
            // h - closepath
            else if (op[0] == 'h' && op[1] == '\0') {
                if (seg_count < MAX_PATH_SEGS &&
                    (path_cx != path_sx || path_cy != path_sy)) {
                    path_segs[seg_count].x1 = path_cx;
                    path_segs[seg_count].y1 = path_cy;
                    path_segs[seg_count].x2 = path_sx;
                    path_segs[seg_count].y2 = path_sy;
                    seg_count++;
                    path_cx = path_sx;
                    path_cy = path_sy;
                }
            }
            // ---- Path painting operators ----
            // S - stroke
            else if (op[0] == 'S' && op[1] == '\0' && !in_text) {
                float lw = gs.line_width * ctm_scale(gs.ctm);
                for (int si = 0; si < seg_count; si++)
                    add_gfx_item(page, GFX_LINE,
                                 path_segs[si].x1, path_segs[si].y1,
                                 path_segs[si].x2, path_segs[si].y2,
                                 lw, gs.stroke_r, gs.stroke_g, gs.stroke_b);
                seg_count = 0;
                rect_count = 0;
            }
            // s - close and stroke
            else if (op[0] == 's' && op[1] == '\0' && !in_text) {
                // Close first
                if (seg_count < MAX_PATH_SEGS &&
                    (path_cx != path_sx || path_cy != path_sy)) {
                    path_segs[seg_count++] = {path_cx, path_cy, path_sx, path_sy};
                }
                float lw = gs.line_width * ctm_scale(gs.ctm);
                for (int si = 0; si < seg_count; si++)
                    add_gfx_item(page, GFX_LINE,
                                 path_segs[si].x1, path_segs[si].y1,
                                 path_segs[si].x2, path_segs[si].y2,
                                 lw, gs.stroke_r, gs.stroke_g, gs.stroke_b);
                seg_count = 0;
                rect_count = 0;
            }
            // f or F - fill
            else if ((op[0] == 'f' || op[0] == 'F') &&
                     (op[1] == '\0' || (op[1] == '*' && op[2] == '\0')) && !in_text) {
                for (int ri = 0; ri < rect_count; ri++)
                    add_gfx_item(page, GFX_RECT_FILL,
                                 path_rects[ri].x, path_rects[ri].y,
                                 path_rects[ri].w, path_rects[ri].h,
                                 0, gs.fill_r, gs.fill_g, gs.fill_b);
                seg_count = 0;
                rect_count = 0;
            }
            // B or B* - fill then stroke
            else if (op[0] == 'B' && (op[1] == '\0' || (op[1] == '*' && op[2] == '\0'))
                     && !in_text) {
                for (int ri = 0; ri < rect_count; ri++)
                    add_gfx_item(page, GFX_RECT_FILL,
                                 path_rects[ri].x, path_rects[ri].y,
                                 path_rects[ri].w, path_rects[ri].h,
                                 0, gs.fill_r, gs.fill_g, gs.fill_b);
                float lw = gs.line_width * ctm_scale(gs.ctm);
                for (int si = 0; si < seg_count; si++)
                    add_gfx_item(page, GFX_LINE,
                                 path_segs[si].x1, path_segs[si].y1,
                                 path_segs[si].x2, path_segs[si].y2,
                                 lw, gs.stroke_r, gs.stroke_g, gs.stroke_b);
                seg_count = 0;
                rect_count = 0;
            }
            // b or b* - close, fill then stroke
            else if (op[0] == 'b' && (op[1] == '\0' || (op[1] == '*' && op[2] == '\0'))
                     && !in_text) {
                if (seg_count < MAX_PATH_SEGS &&
                    (path_cx != path_sx || path_cy != path_sy))
                    path_segs[seg_count++] = {path_cx, path_cy, path_sx, path_sy};
                for (int ri = 0; ri < rect_count; ri++)
                    add_gfx_item(page, GFX_RECT_FILL,
                                 path_rects[ri].x, path_rects[ri].y,
                                 path_rects[ri].w, path_rects[ri].h,
                                 0, gs.fill_r, gs.fill_g, gs.fill_b);
                float lw = gs.line_width * ctm_scale(gs.ctm);
                for (int si = 0; si < seg_count; si++)
                    add_gfx_item(page, GFX_LINE,
                                 path_segs[si].x1, path_segs[si].y1,
                                 path_segs[si].x2, path_segs[si].y2,
                                 lw, gs.stroke_r, gs.stroke_g, gs.stroke_b);
                seg_count = 0;
                rect_count = 0;
            }
            // n - end path without painting (clipping only)
            else if (op[0] == 'n' && op[1] == '\0' && !in_text) {
                seg_count = 0;
                rect_count = 0;
            }

            op_count = 0; // reset operand stack after operator
        }

        montauk::mfree(ops);
        if (path_segs) montauk::mfree(path_segs);
        if (path_rects) montauk::mfree(path_rects);
        montauk::mfree(stream_data);
    }

    for (int fi = 0; fi < fonts->count; fi++)
        if (fonts->fonts[fi].tounicode) montauk::mfree(fonts->fonts[fi].tounicode);
    montauk::mfree(fonts);
}
