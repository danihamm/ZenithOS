/*
 * formula.cpp
 * Formula parser, cell evaluation, and geometry helpers
 * Copyright (c) 2026 Daniel Hammer
 */

#include "spreadsheet.h"

// ============================================================================
// Formula name list (for autocomplete and parsing)
// ============================================================================

struct FormulaInfo {
    const char* name;
    const char* hint;
};

static const FormulaInfo g_formula_list[] = {
    {"SUM",   "SUM(range)"},
    {"AVG",   "AVG(range)"},
    {"MIN",   "MIN(range)"},
    {"MAX",   "MAX(range)"},
    {"COUNT", "COUNT(range)"},
    {"ABS",   "ABS(value)"},
    {"SQRT",  "SQRT(value)"},
    {"ROUND", "ROUND(val,n)"},
    {"INT",   "INT(value)"},
    {"FLOOR", "FLOOR(value)"},
    {"CEIL",  "CEIL(value)"},
    {"POW",   "POW(base,exp)"},
    {"MOD",   "MOD(a,b)"},
    {"PI",    "PI()"},
    {"IF",    "IF(cond,t,f)"},
};
static constexpr int FORMULA_COUNT = sizeof(g_formula_list) / sizeof(g_formula_list[0]);

// ============================================================================
// Cell reference parsing
// ============================================================================

static bool parse_cell_ref(const char* s, int* col, int* row, int* consumed) {
    int i = 0;
    char c = to_upper(s[i]);
    if (c < 'A' || c > 'Z') return false;
    *col = c - 'A';
    i++;

    if (!is_digit(s[i])) return false;
    int r = 0;
    while (is_digit(s[i])) {
        r = r * 10 + (s[i] - '0');
        i++;
    }
    if (r < 1 || r > MAX_ROWS) return false;
    *row = r - 1;
    *consumed = i;
    return true;
}

// ============================================================================
// Formula evaluation (simple recursive-descent)
// ============================================================================

static double eval_expr(const char* s, int* pos, bool* ok);

static double cell_value(int col, int row) {
    if (col < 0 || col >= MAX_COLS || row < 0 || row >= MAX_ROWS) return 0;
    return g_cells[row][col].value;
}

static double eval_range_func(const char* func, const char* s, int* pos, bool* ok) {
    if (s[*pos] != '(') { *ok = false; return 0; }
    (*pos)++;

    int c1, r1, c2, r2, consumed;
    if (!parse_cell_ref(s + *pos, &c1, &r1, &consumed)) { *ok = false; return 0; }
    *pos += consumed;

    if (s[*pos] != ':') { *ok = false; return 0; }
    (*pos)++;

    if (!parse_cell_ref(s + *pos, &c2, &r2, &consumed)) { *ok = false; return 0; }
    *pos += consumed;

    if (s[*pos] != ')') { *ok = false; return 0; }
    (*pos)++;

    if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }

    double sum = 0;
    int count = 0;
    for (int r = r1; r <= r2; r++) {
        for (int c = c1; c <= c2; c++) {
            sum += cell_value(c, r);
            count++;
        }
    }

    bool is_sum = (func[0] == 'S' || func[0] == 's');
    bool is_avg = (func[0] == 'A' || func[0] == 'a');
    bool is_min = (func[0] == 'M' || func[0] == 'm') && (func[1] == 'I' || func[1] == 'i');
    bool is_max = (func[0] == 'M' || func[0] == 'm') && (func[1] == 'A' || func[1] == 'a');
    bool is_cnt = (func[0] == 'C' || func[0] == 'c');

    if (is_sum) return sum;
    if (is_avg) return count > 0 ? sum / count : 0;
    if (is_cnt) return (double)count;
    if (is_min || is_max) {
        double result = cell_value(c1, r1);
        for (int r = r1; r <= r2; r++) {
            for (int c = c1; c <= c2; c++) {
                double v = cell_value(c, r);
                if (is_min && v < result) result = v;
                if (is_max && v > result) result = v;
            }
        }
        return result;
    }

    *ok = false;
    return 0;
}

static void skip_spaces(const char* s, int* pos) {
    while (s[*pos] == ' ') (*pos)++;
}

static double eval_primary(const char* s, int* pos, bool* ok) {
    skip_spaces(s, pos);

    if (s[*pos] == '-') {
        (*pos)++;
        return -eval_primary(s, pos, ok);
    }

    if (s[*pos] == '(') {
        (*pos)++;
        double v = eval_expr(s, pos, ok);
        skip_spaces(s, pos);
        if (s[*pos] == ')') (*pos)++;
        return v;
    }

    if (is_alpha(s[*pos])) {
        char fname[8] = {};
        int fi = 0;
        int save_pos = *pos;
        while (is_alpha(s[*pos]) && fi < 7) {
            fname[fi++] = to_upper(s[*pos]);
            (*pos)++;
        }
        fname[fi] = '\0';

        // Range functions: SUM, AVG, MIN, MAX, COUNT
        if (s[*pos] == '(' && (strcmp(fname, "SUM") == 0 ||
                                strcmp(fname, "AVG") == 0 ||
                                strcmp(fname, "MIN") == 0 ||
                                strcmp(fname, "MAX") == 0 ||
                                strcmp(fname, "COUNT") == 0)) {
            return eval_range_func(fname, s, pos, ok);
        }

        // No-arg: PI()
        if (s[*pos] == '(' && strcmp(fname, "PI") == 0) {
            (*pos)++;
            skip_spaces(s, pos);
            if (s[*pos] == ')') (*pos)++;
            return 3.14159265358979;
        }

        // Single-arg functions
        if (s[*pos] == '(' && (strcmp(fname, "ABS") == 0 ||
                                strcmp(fname, "SQRT") == 0 ||
                                strcmp(fname, "INT") == 0 ||
                                strcmp(fname, "FLOOR") == 0 ||
                                strcmp(fname, "CEIL") == 0)) {
            (*pos)++;
            double arg = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] == ')') (*pos)++;
            if (!*ok) return 0;
            if (strcmp(fname, "ABS") == 0)   return stb_fabs(arg);
            if (strcmp(fname, "SQRT") == 0)  return stb_sqrt(arg);
            if (strcmp(fname, "INT") == 0)   return (double)(long long)arg;
            if (strcmp(fname, "FLOOR") == 0) return stb_floor(arg);
            if (strcmp(fname, "CEIL") == 0)  return stb_ceil(arg);
            return 0;
        }

        // Two-arg functions: ROUND, POW, MOD
        if (s[*pos] == '(' && (strcmp(fname, "ROUND") == 0 ||
                                strcmp(fname, "POW") == 0 ||
                                strcmp(fname, "MOD") == 0)) {
            (*pos)++;
            double a = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] != ',') { *ok = false; return 0; }
            (*pos)++;
            double b = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] == ')') (*pos)++;
            if (!*ok) return 0;
            if (strcmp(fname, "POW") == 0) return stb_pow(a, b);
            if (strcmp(fname, "MOD") == 0) {
                if (b == 0) { *ok = false; return 0; }
                return stb_fmod(a, b);
            }
            if (strcmp(fname, "ROUND") == 0) {
                double factor = stb_pow(10.0, b);
                return stb_floor(a * factor + 0.5) / factor;
            }
            return 0;
        }

        // IF(condition, true_value, false_value)
        if (s[*pos] == '(' && strcmp(fname, "IF") == 0) {
            (*pos)++;
            double cond = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] != ',') { *ok = false; return 0; }
            (*pos)++;
            double tv = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] != ',') { *ok = false; return 0; }
            (*pos)++;
            double fv = eval_expr(s, pos, ok);
            skip_spaces(s, pos);
            if (s[*pos] == ')') (*pos)++;
            if (!*ok) return 0;
            return (cond != 0.0) ? tv : fv;
        }

        // Fall back to cell reference
        *pos = save_pos;
        int col, row, consumed;
        if (parse_cell_ref(s + *pos, &col, &row, &consumed)) {
            *pos += consumed;
            return cell_value(col, row);
        }

        *ok = false;
        return 0;
    }

    if (is_digit(s[*pos]) || s[*pos] == '.') {
        double result = 0;
        while (is_digit(s[*pos])) {
            result = result * 10 + (s[*pos] - '0');
            (*pos)++;
        }
        if (s[*pos] == '.') {
            (*pos)++;
            double frac = 0.1;
            while (is_digit(s[*pos])) {
                result += (s[*pos] - '0') * frac;
                frac *= 0.1;
                (*pos)++;
            }
        }
        return result;
    }

    *ok = false;
    return 0;
}

static double eval_term(const char* s, int* pos, bool* ok) {
    double left = eval_primary(s, pos, ok);
    while (*ok) {
        skip_spaces(s, pos);
        char op = s[*pos];
        if (op != '*' && op != '/') break;
        (*pos)++;
        double right = eval_primary(s, pos, ok);
        if (op == '*') left *= right;
        else if (right != 0) left /= right;
        else { *ok = false; return 0; }
    }
    return left;
}

static double eval_expr(const char* s, int* pos, bool* ok) {
    double left = eval_term(s, pos, ok);
    while (*ok) {
        skip_spaces(s, pos);
        char op = s[*pos];
        if (op != '+' && op != '-') break;
        (*pos)++;
        double right = eval_term(s, pos, ok);
        if (op == '+') left += right;
        else left -= right;
    }
    return left;
}

// ============================================================================
// Cell evaluation
// ============================================================================

void format_value(char* buf, int max, double val, NumFormat fmt) {
    switch (fmt) {
    case FMT_CURRENCY: {
        bool neg = val < 0;
        double av = neg ? -val : val;
        long long cents = (long long)(av * 100 + 0.5);
        long long dollars = cents / 100;
        int c = (int)(cents % 100);
        if (neg)
            snprintf(buf, max, "-$%lld.%02d", dollars, c);
        else
            snprintf(buf, max, "$%lld.%02d", dollars, c);
        break;
    }
    case FMT_PERCENT: {
        double av = val < 0 ? -val : val;
        long long ip = (long long)av;
        double frac = av - ip;
        if (frac < 0.005)
            snprintf(buf, max, "%s%lld%%", val < 0 ? "-" : "", ip);
        else
            snprintf(buf, max, "%s%lld.%02d%%", val < 0 ? "-" : "", ip, (int)(frac * 100 + 0.5));
        break;
    }
    case FMT_DECIMAL: {
        long long rounded = (long long)((val < 0 ? -val : val) * 100 + 0.5);
        long long ip = rounded / 100;
        int dp = (int)(rounded % 100);
        snprintf(buf, max, "%s%lld.%02d", val < 0 ? "-" : "", ip, dp);
        break;
    }
    default:
        double_to_str(buf, max, val);
        break;
    }
}

void eval_cell(int col, int row) {
    Cell* c = &g_cells[row][col];
    if (c->input[0] == '\0') {
        c->type = CT_EMPTY;
        c->display[0] = '\0';
        c->value = 0;
        return;
    }

    if (c->input[0] == '=') {
        int pos = 1;
        bool ok = true;
        double val = eval_expr(c->input, &pos, &ok);
        if (ok) {
            c->type = CT_FORMULA;
            c->value = val;
            format_value(c->display, CELL_TEXT_MAX, val, c->fmt);
        } else {
            c->type = CT_ERROR;
            c->value = 0;
            str_cpy(c->display, "#ERR", CELL_TEXT_MAX);
        }
        return;
    }

    bool ok;
    double val = str_to_double(c->input, &ok);
    if (ok) {
        c->type = CT_NUMBER;
        c->value = val;
        format_value(c->display, CELL_TEXT_MAX, val, c->fmt);
    } else {
        c->type = CT_TEXT;
        c->value = 0;
        str_cpy(c->display, c->input, CELL_TEXT_MAX);
    }
}

void eval_all_cells() {
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            eval_cell(c, r);
}

// ============================================================================
// Column/row geometry helpers
// ============================================================================

int col_x(int col) {
    int x = ROW_HEADER_W;
    for (int i = 0; i < col; i++) x += g_col_widths[i];
    return x;
}

int content_width() {
    int w = ROW_HEADER_W;
    for (int i = 0; i < MAX_COLS; i++) w += g_col_widths[i];
    return w;
}

int content_height() {
    return COL_HEADER_H + MAX_ROWS * ROW_H;
}

void cell_name(char* buf, int col, int row) {
    buf[0] = 'A' + col;
    int r = row + 1;
    if (r >= 100) { buf[1] = '0' + r / 100; buf[2] = '0' + (r / 10) % 10; buf[3] = '0' + r % 10; buf[4] = '\0'; }
    else if (r >= 10) { buf[1] = '0' + r / 10; buf[2] = '0' + r % 10; buf[3] = '\0'; }
    else { buf[1] = '0' + r; buf[2] = '\0'; }
}

// ============================================================================
// Formula autocomplete
// ============================================================================

void update_autocomplete() {
    g_ac_open = false;
    g_ac_count = 0;

    if (!g_editing || g_edit_buf[0] != '=') return;

    // Find start of current alphabetic word at cursor
    int word_start = g_edit_cursor;
    while (word_start > 0 && is_alpha(g_edit_buf[word_start - 1]))
        word_start--;

    int word_len = g_edit_cursor - word_start;
    if (word_len == 0 || word_len > 7) return;

    // Don't autocomplete if cursor is right before more alpha chars (mid-word)
    if (g_edit_cursor < g_edit_len && is_alpha(g_edit_buf[g_edit_cursor])) return;

    // Don't show if next char is '(' -- user already completed the name
    if (g_edit_cursor < g_edit_len && g_edit_buf[g_edit_cursor] == '(') return;

    char partial[8];
    for (int i = 0; i < word_len; i++)
        partial[i] = to_upper(g_edit_buf[word_start + i]);
    partial[word_len] = '\0';

    for (int i = 0; i < FORMULA_COUNT && g_ac_count < AC_MAX_MATCHES; i++) {
        const char* name = g_formula_list[i].name;
        bool match = true;
        for (int j = 0; j < word_len; j++) {
            if (name[j] == '\0' || to_upper(name[j]) != partial[j]) {
                match = false;
                break;
            }
        }
        // Don't show exact matches (already fully typed)
        if (match && name[word_len] != '\0') {
            str_cpy(g_ac_matches[g_ac_count], name, 16);
            str_cpy(g_ac_hints[g_ac_count], g_formula_list[i].hint, 32);
            g_ac_count++;
        }
    }

    if (g_ac_count > 0) {
        g_ac_open = true;
        g_ac_sel = 0;
    }
}

void accept_autocomplete() {
    if (!g_ac_open || g_ac_sel < 0 || g_ac_sel >= g_ac_count) return;

    // Find word start
    int word_start = g_edit_cursor;
    while (word_start > 0 && is_alpha(g_edit_buf[word_start - 1]))
        word_start--;

    const char* name = g_ac_matches[g_ac_sel];
    int name_len = str_len(name);

    int old_len = g_edit_cursor - word_start;
    int new_len = name_len + 1; // +1 for '('
    int delta = new_len - old_len;

    // Check buffer space
    if (g_edit_len + delta >= CELL_TEXT_MAX - 1) { g_ac_open = false; return; }

    // Shift rest of buffer
    if (delta > 0) {
        for (int i = g_edit_len; i >= g_edit_cursor; i--)
            g_edit_buf[i + delta] = g_edit_buf[i];
    } else if (delta < 0) {
        for (int i = g_edit_cursor; i <= g_edit_len; i++)
            g_edit_buf[i + delta] = g_edit_buf[i];
    }

    // Write function name + opening paren
    for (int i = 0; i < name_len; i++)
        g_edit_buf[word_start + i] = name[i];
    g_edit_buf[word_start + name_len] = '(';

    g_edit_len += delta;
    g_edit_cursor = word_start + new_len;
    g_edit_buf[g_edit_len] = '\0';

    g_ac_open = false;
}

// ============================================================================
// Formula reference adjustment (for fill handle)
// ============================================================================

void adjust_formula_refs(const char* src, char* dst, int max, int dcol, int drow) {
    int si = 0, di = 0;
    while (src[si] && di < max - 1) {
        if (is_alpha(src[si])) {
            // Try to parse a cell reference
            int col, row, consumed;
            if (parse_cell_ref(src + si, &col, &row, &consumed)) {
                // Check this is actually a cell ref and not part of a function name
                // by looking backward: if the previous char is also alpha, it's a
                // function name, not a cell ref
                bool is_func_part = (si > 0 && is_alpha(src[si - 1]));
                if (is_func_part) {
                    dst[di++] = src[si++];
                    continue;
                }
                int nc = col + dcol;
                int nr = row + drow;
                if (nc < 0) nc = 0;
                if (nc >= MAX_COLS) nc = MAX_COLS - 1;
                if (nr < 0) nr = 0;
                if (nr >= MAX_ROWS) nr = MAX_ROWS - 1;
                dst[di++] = 'A' + nc;
                int rv = nr + 1;
                if (rv >= 100 && di < max - 3) {
                    dst[di++] = '0' + rv / 100;
                    dst[di++] = '0' + (rv / 10) % 10;
                    dst[di++] = '0' + rv % 10;
                } else if (rv >= 10 && di < max - 2) {
                    dst[di++] = '0' + rv / 10;
                    dst[di++] = '0' + rv % 10;
                } else {
                    dst[di++] = '0' + rv;
                }
                si += consumed;
            } else {
                dst[di++] = src[si++];
            }
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}
