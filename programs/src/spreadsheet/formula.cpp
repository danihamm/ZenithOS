/*
 * formula.cpp
 * Formula parser, cell evaluation, and geometry helpers
 * Copyright (c) 2026 Daniel Hammer
 */

#include "spreadsheet.h"

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

        if (s[*pos] == '(' && (strcmp(fname, "SUM") == 0 ||
                                strcmp(fname, "AVG") == 0 ||
                                strcmp(fname, "MIN") == 0 ||
                                strcmp(fname, "MAX") == 0 ||
                                strcmp(fname, "COUNT") == 0)) {
            return eval_range_func(fname, s, pos, ok);
        }

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
