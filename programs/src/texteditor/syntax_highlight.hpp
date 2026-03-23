/*
    * syntax_highlight.hpp
    * C syntax highlighting for the MontaukOS text editor
    * Activated for .c and .h files
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once

#include <gui/gui.hpp>

using namespace gui;

// ============================================================================
// Token types
// ============================================================================

enum SynToken : uint8_t {
    SYN_NORMAL,
    SYN_KEYWORD,
    SYN_TYPE,
    SYN_PREPROCESSOR,
    SYN_STRING,
    SYN_CHAR,
    SYN_COMMENT,
    SYN_NUMBER,
    SYN_OPERATOR,
};

// ============================================================================
// Colors for each token type
// ============================================================================

inline Color syn_color(SynToken tok) {
    switch (tok) {
    case SYN_KEYWORD:      return Color::from_rgb(0xC5, 0x62, 0x8C); // magenta-pink
    case SYN_TYPE:          return Color::from_rgb(0x2E, 0x86, 0xAB); // teal
    case SYN_PREPROCESSOR:  return Color::from_rgb(0x8B, 0x6E, 0xB5); // purple
    case SYN_STRING:        return Color::from_rgb(0x6A, 0x9F, 0x3A); // green
    case SYN_CHAR:          return Color::from_rgb(0x6A, 0x9F, 0x3A); // green
    case SYN_COMMENT:       return Color::from_rgb(0x7A, 0x7A, 0x7A); // gray
    case SYN_NUMBER:        return Color::from_rgb(0xC9, 0x7E, 0x2A); // orange
    case SYN_OPERATOR:      return Color::from_rgb(0x40, 0x40, 0x40); // dark gray
    default:                return Color::from_rgb(0x1E, 0x1E, 0x1E); // near-black
    }
}

// ============================================================================
// Keyword / type lookup
// ============================================================================

inline bool syn_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool syn_is_alnum(char c) {
    return syn_is_alpha(c) || (c >= '0' && c <= '9');
}

inline bool syn_is_digit(char c) {
    return c >= '0' && c <= '9';
}

inline bool syn_is_hex(char c) {
    return syn_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool syn_streq(const char* buf, int len, const char* kw) {
    int i = 0;
    while (i < len && kw[i]) {
        if (buf[i] != kw[i]) return false;
        i++;
    }
    return i == len && kw[i] == '\0';
}

inline SynToken syn_classify_word(const char* buf, int len) {
    // C keywords
    static const char* keywords[] = {
        "auto", "break", "case", "const", "continue", "default", "do",
        "else", "enum", "extern", "for", "goto", "if", "inline",
        "register", "restrict", "return", "sizeof", "static", "struct",
        "switch", "typedef", "union", "volatile", "while",
        "NULL", "true", "false", "nullptr",
    };
    // C types
    static const char* types[] = {
        "void", "char", "short", "int", "long", "float", "double",
        "signed", "unsigned", "bool", "_Bool",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        "FILE",
    };

    for (int i = 0; i < (int)(sizeof(keywords) / sizeof(keywords[0])); i++) {
        if (syn_streq(buf, len, keywords[i])) return SYN_KEYWORD;
    }
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        if (syn_streq(buf, len, types[i])) return SYN_TYPE;
    }
    return SYN_NORMAL;
}

// ============================================================================
// Per-line highlighter
// ============================================================================
//
// Fills `out[]` with SynToken values for each character in the line.
// `in_block_comment` is the multi-line comment state carried across lines:
//   - pass in the state from the previous line
//   - on return, updated for the next line
//
// `line` points to the first char of the line, `len` is its length
// (excluding the newline). `out` must have room for `len` entries.

inline void syn_highlight_line(const char* line, int len, SynToken* out, bool& in_block_comment) {
    int i = 0;

    while (i < len) {
        // ---- Block comment continuation ----
        if (in_block_comment) {
            while (i < len) {
                if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                    out[i] = SYN_COMMENT;
                    out[i + 1] = SYN_COMMENT;
                    i += 2;
                    in_block_comment = false;
                    break;
                }
                out[i] = SYN_COMMENT;
                i++;
            }
            continue;
        }

        char c = line[i];

        // ---- Line comment ----
        if (c == '/' && i + 1 < len && line[i + 1] == '/') {
            while (i < len) out[i++] = SYN_COMMENT;
            break;
        }

        // ---- Block comment start ----
        if (c == '/' && i + 1 < len && line[i + 1] == '*') {
            in_block_comment = true;
            out[i] = SYN_COMMENT;
            out[i + 1] = SYN_COMMENT;
            i += 2;
            continue;
        }

        // ---- Preprocessor directive ----
        if (c == '#') {
            // Check that only whitespace precedes the #
            bool is_pp = true;
            for (int j = 0; j < i; j++) {
                if (line[j] != ' ' && line[j] != '\t') { is_pp = false; break; }
            }
            if (is_pp) {
                while (i < len) {
                    // Handle line-comment inside preprocessor
                    if (i + 1 < len && line[i] == '/' && line[i + 1] == '/') {
                        while (i < len) out[i++] = SYN_COMMENT;
                        break;
                    }
                    // Handle block comment start inside preprocessor
                    if (i + 1 < len && line[i] == '/' && line[i + 1] == '*') {
                        in_block_comment = true;
                        out[i] = SYN_COMMENT;
                        out[i + 1] = SYN_COMMENT;
                        i += 2;
                        // Continue consuming as comment
                        while (i < len) {
                            if (i + 1 < len && line[i] == '*' && line[i + 1] == '/') {
                                out[i] = SYN_COMMENT;
                                out[i + 1] = SYN_COMMENT;
                                i += 2;
                                in_block_comment = false;
                                break;
                            }
                            out[i] = SYN_COMMENT;
                            i++;
                        }
                        continue;
                    }
                    out[i] = SYN_PREPROCESSOR;
                    i++;
                }
                continue;
            }
        }

        // ---- String literal ----
        if (c == '"') {
            out[i++] = SYN_STRING;
            while (i < len) {
                if (line[i] == '\\' && i + 1 < len) {
                    out[i] = SYN_STRING;
                    out[i + 1] = SYN_STRING;
                    i += 2;
                    continue;
                }
                if (line[i] == '"') {
                    out[i++] = SYN_STRING;
                    break;
                }
                out[i++] = SYN_STRING;
            }
            continue;
        }

        // ---- Character literal ----
        if (c == '\'') {
            out[i++] = SYN_CHAR;
            while (i < len) {
                if (line[i] == '\\' && i + 1 < len) {
                    out[i] = SYN_CHAR;
                    out[i + 1] = SYN_CHAR;
                    i += 2;
                    continue;
                }
                if (line[i] == '\'') {
                    out[i++] = SYN_CHAR;
                    break;
                }
                out[i++] = SYN_CHAR;
            }
            continue;
        }

        // ---- Numbers ----
        if (syn_is_digit(c) || (c == '.' && i + 1 < len && syn_is_digit(line[i + 1]))) {
            // Hex
            if (c == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                out[i] = SYN_NUMBER; out[i + 1] = SYN_NUMBER;
                i += 2;
                while (i < len && syn_is_hex(line[i])) out[i++] = SYN_NUMBER;
            } else {
                // Decimal / float
                while (i < len && (syn_is_digit(line[i]) || line[i] == '.'))
                    out[i++] = SYN_NUMBER;
            }
            // Suffixes: u, l, f, etc.
            while (i < len && (line[i] == 'u' || line[i] == 'U' ||
                               line[i] == 'l' || line[i] == 'L' ||
                               line[i] == 'f' || line[i] == 'F'))
                out[i++] = SYN_NUMBER;
            continue;
        }

        // ---- Identifiers / keywords / types ----
        if (syn_is_alpha(c)) {
            int start = i;
            while (i < len && syn_is_alnum(line[i])) i++;
            SynToken tok = syn_classify_word(line + start, i - start);
            for (int j = start; j < i; j++) out[j] = tok;
            continue;
        }

        // ---- Operators ----
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '=' || c == '!' || c == '<' || c == '>' || c == '&' ||
            c == '|' || c == '^' || c == '~' || c == '?' || c == ':') {
            out[i++] = SYN_OPERATOR;
            continue;
        }

        // ---- Everything else (whitespace, braces, parens, etc.) ----
        out[i++] = SYN_NORMAL;
    }
}

// ============================================================================
// Extension check
// ============================================================================

inline bool syn_is_c_file(const char* filepath) {
    if (!filepath || filepath[0] == '\0') return false;
    int len = 0;
    while (filepath[len]) len++;
    if (len >= 2 && filepath[len - 2] == '.' &&
        (filepath[len - 1] == 'c' || filepath[len - 1] == 'h'))
        return true;
    return false;
}
