#include "sheet.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================== the cells ===================== */

/* In position order (row by row), so a lookup is a binary search and a
 * range is one stretch of the array. */
typedef struct {
    px_dec_t num;  /* a number's value, a formula's result */
    char *text;    /* as typed (malloc'd) */
    uint16_t pos;  /* row * SHEET_COLS + col */
    uint8_t kind;  /* cell_kind_t */
    uint8_t err;   /* a formula's error (sheet_err_t) */
    bool pending;  /* a formula sheet_recalc() hasn't worked out yet */
} cell_t;

static cell_t cells[SHEET_CELLS_MAX];
static int ncells;

/* The cell at pos: its index, or -(where it would go) - 1. */
static int find(int pos) {
    int lo = 0, hi = ncells;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cells[mid].pos < pos)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < ncells && cells[lo].pos == pos ? lo : -lo - 1;
}

static px_dec_t from_int(int v) {
    px_dec_t r;
    px->dec_from_int(&r, v);
    return r;
}

static bool is_zero(const px_dec_t *a) {
    px_dec_t zero = from_int(0);
    return px->dec_cmp(a, &zero) == 0;
}

/* The whole of s, spaces around it allowed, as a number: [+-]digits[.digits][E[+-]digits]. */
static bool parse_number(const char *s, px_dec_t *v) {
    char buf[SHEET_INPUT_MAX + 1];
    int n = 0, digits = 0;
    while (*s == ' ')
        s++;
    if (*s == '+' || *s == '-') {
        if (*s == '-')
            buf[n++] = '-';
        s++;
    }
    for (; isdigit((unsigned char) *s); s++, digits++)
        buf[n++] = *s;
    if (*s == '.') {
        buf[n++] = *s++;
        for (; isdigit((unsigned char) *s); s++, digits++)
            buf[n++] = *s;
    }
    if (digits == 0)
        return false;
    if ((*s == 'e' || *s == 'E') && (isdigit((unsigned char) s[1]) || ((s[1] == '+' || s[1] == '-') && isdigit((unsigned char) s[2])))) {
        buf[n++] = 'E';
        s++;
        if (*s == '+' || *s == '-')
            buf[n++] = *s++;
        while (isdigit((unsigned char) *s) && n < SHEET_INPUT_MAX)
            buf[n++] = *s++;
    }
    while (*s == ' ')
        s++;
    if (*s != 0)
        return false;
    buf[n] = 0;
    return px->dec_from_string(v, buf) == 0;
}

void sheet_clear(void) {
    for (int i = 0; i < ncells; i++)
        free(cells[i].text);
    ncells = 0;
}

bool sheet_set(int row, int col, const char *input) {
    int pos = row * SHEET_COLS + col;
    int i = find(pos);
    if (input[0] == 0) {
        if (i >= 0) {
            free(cells[i].text);
            memmove(&cells[i], &cells[i + 1], (size_t) (ncells - i - 1) * sizeof(cell_t));
            ncells--;
        }
        return true;
    }
    size_t len = strlen(input);
    if (len > SHEET_INPUT_MAX)
        len = SHEET_INPUT_MAX;
    char *text = malloc(len + 1);
    if (text == NULL)
        return false;
    memcpy(text, input, len);
    text[len] = 0;
    if (i < 0) {
        if (ncells == SHEET_CELLS_MAX) {
            free(text);
            return false;
        }
        i = -i - 1;
        memmove(&cells[i + 1], &cells[i], (size_t) (ncells - i) * sizeof(cell_t));
        ncells++;
        cells[i].text = NULL;
        cells[i].pos = (uint16_t) pos;
    }
    cell_t *c = &cells[i];
    free(c->text);
    c->text = text;
    c->err = SHEET_OK;
    c->pending = false;
    c->num = from_int(0);
    if (text[0] == '=')
        c->kind = CELL_FORMULA;
    else if (parse_number(text, &c->num))
        c->kind = CELL_NUMBER;
    else
        c->kind = CELL_TEXT;
    return true;
}

const char *sheet_input(int row, int col) {
    int i = find(row * SHEET_COLS + col);
    return i >= 0 ? cells[i].text : "";
}

void sheet_value(int row, int col, sheet_value_t *v) {
    int i = find(row * SHEET_COLS + col);
    memset(v, 0, sizeof(*v));
    if (i < 0)
        return;
    const cell_t *c = &cells[i];
    v->kind = (cell_kind_t) c->kind;
    v->err = (sheet_err_t) c->err;
    v->num = c->num;
    v->text = c->text[0] == '\'' ? c->text + 1 : c->text;
}

int sheet_count(void) {
    return ncells;
}

void sheet_extent(int *last_row, int *last_col) {
    *last_row = ncells > 0 ? cells[ncells - 1].pos / SHEET_COLS : -1;
    *last_col = -1;
    for (int i = 0; i < ncells; i++)
        if (cells[i].pos % SHEET_COLS > *last_col)
            *last_col = cells[i].pos % SHEET_COLS;
}

/* ===================== cell names ===================== */

#define ABS_COL 1
#define ABS_ROW 2

/* A cell's name at s: [$]letter[$]digits, not followed by a letter, digit
 * or '(' (LOG10( is a function). The characters it takes, or 0. *col is
 * -1 for a name past the sheet (AA1, Z1000). */
static int ref_at(const char *s, int *row, int *col, int *abs) {
    int i = 0, letters, r = 0, digits = 0;
    *abs = 0;
    if (s[i] == '$') {
        *abs |= ABS_COL;
        i++;
    }
    if (!isalpha((unsigned char) s[i]))
        return 0;
    int first = i;
    while (isalpha((unsigned char) s[i]))
        i++;
    letters = i - first;
    if (s[i] == '$') {
        *abs |= ABS_ROW;
        i++;
    }
    for (; isdigit((unsigned char) s[i]); i++, digits++)
        if (digits < 6)
            r = r * 10 + s[i] - '0';
    if (digits == 0 || isalnum((unsigned char) s[i]) || s[i] == '_' || s[i] == '(')
        return 0;
    if (letters != 1 || r < 1 || r > SHEET_ROWS) {
        *col = -1;
        *row = -1;
    } else {
        *col = toupper((unsigned char) s[first]) - 'A';
        *row = r - 1;
    }
    return i;
}

int sheet_parse_ref(const char *s, int *row, int *col) {
    int abs;
    int n = ref_at(s, row, col, &abs);
    return n > 0 && *col >= 0 ? n : 0;
}

void sheet_ref_name(char *buf, int row, int col) {
    snprintf(buf, 6, "%c%u", 'A' + col, (unsigned) (row + 1) % 1000u);
}

/* ===================== formulas =====================
 * Read and worked out in one go, by recursive descent, from Excel's
 * precedence: comparisons, + -, * /, ^ (left to right), - in front, %.
 * An error jumps out with its code (fail()); so does a cell another
 * formula needs that isn't worked out yet (BLOCKED: try again next pass).
 * In an IF's branch not taken (`skipping`), it's only read. */

#define BLOCKED   100
#define DEPTH_MAX 16 /* parentheses and calls inside each other: the stack is the firmware's */

static const char *p;
static int skipping, depth;
static jmp_buf jump;

static _Noreturn void fail(int err) {
    longjmp(jump, err);
}

static void spaces(void) {
    while (*p == ' ')
        p++;
}

static bool eat(char c) {
    spaces();
    if (*p != c)
        return false;
    p++;
    return true;
}

static void expect(char c) {
    if (!eat(c))
        fail(SHEET_SYNTAX);
}

/* A result that's infinite or not a number is #NUM!. */
static px_dec_t checked(px_dec_t r) {
    if (!skipping && px->dec_class(&r) != PX_DEC_FINITE)
        fail(SHEET_NUM);
    return r;
}

static px_dec_t op2(int op, px_dec_t a, px_dec_t b) {
    if (skipping)
        return a;
    px_dec_t r;
    px->dec(op, &r, &a, &b);
    return checked(r);
}

static px_dec_t op1(int op, px_dec_t a) {
    if (skipping)
        return a;
    px_dec_t r;
    px->dec(op, &r, &a, NULL);
    return checked(r);
}

static px_dec_t divide(px_dec_t a, px_dec_t b) {
    if (!skipping && is_zero(&b))
        fail(SHEET_DIV0);
    return op2(PX_DEC_DIV, a, b);
}

static px_dec_t power(px_dec_t a, px_dec_t b) {
    if (!skipping && is_zero(&a)) {
        px_dec_t zero = from_int(0);
        int c = px->dec_cmp(&b, &zero);
        if (c == 0)
            fail(SHEET_NUM);  /* 0^0, as Excel */
        if (c < 0)
            fail(SHEET_DIV0); /* 0^-1 */
    }
    return op2(PX_DEC_POW, a, b);
}

/* Cell pos for a formula: 0 empty, 1 a number (in *v), 2 text. */
static int cell_get(int pos, px_dec_t *v) {
    int i = find(pos);
    if (i < 0)
        return 0;
    const cell_t *c = &cells[i];
    switch (c->kind) {
        case CELL_NUMBER:
            *v = c->num;
            return 1;
        case CELL_FORMULA:
            if (c->pending)
                fail(BLOCKED);
            if (c->err != SHEET_OK)
                fail(c->err);
            *v = c->num;
            return 1;
        default:
            return 2;
    }
}

/* A name or a range at p: A1 or A1:B5. false (p as it was) if there's none. */
static bool read_range(int *r0, int *c0, int *r1, int *c1) {
    int abs;
    spaces();
    int n = ref_at(p, r0, c0, &abs);
    if (n == 0)
        return false;
    p += n;
    *r1 = *r0;
    *c1 = *c0;
    spaces();
    if (*p == ':') {
        p++;
        spaces();
        n = ref_at(p, r1, c1, &abs);
        if (n == 0)
            fail(SHEET_SYNTAX);
        p += n;
    }
    if (!skipping && (*c0 < 0 || *c1 < 0))
        fail(SHEET_REF);
    return true;
}

/* What the functions over many values collect. */
typedef struct {
    px_dec_t sum, sumsq, prod, min, max;
    int n;
    bool all, any;
} agg_t;

static void agg_add(agg_t *g, px_dec_t v) {
    if (g->n == 0) {
        g->min = g->max = v;
    } else {
        if (px->dec_cmp(&v, &g->min) < 0)
            g->min = v;
        if (px->dec_cmp(&v, &g->max) > 0)
            g->max = v;
    }
    g->sum = op2(PX_DEC_ADD, g->sum, v);
    g->sumsq = op2(PX_DEC_ADD, g->sumsq, op2(PX_DEC_MUL, v, v));
    g->prod = op2(PX_DEC_MUL, g->prod, v);
    if (is_zero(&v))
        g->all = false;
    else
        g->any = true;
    g->n++;
}

/* The numbers in a range; text and empty cells are left out, as in Excel. */
static void agg_range(agg_t *g, int r0, int c0, int r1, int c1) {
    if (r0 > r1) {
        int t = r0;
        r0 = r1;
        r1 = t;
    }
    if (c0 > c1) {
        int t = c0;
        c0 = c1;
        c1 = t;
    }
    int i = find(r0 * SHEET_COLS + c0);
    if (i < 0)
        i = -i - 1;
    for (; i < ncells && cells[i].pos <= r1 * SHEET_COLS + c1; i++) {
        int col = cells[i].pos % SHEET_COLS;
        px_dec_t v;
        if (col >= c0 && col <= c1 && cell_get(cells[i].pos, &v) == 1)
            agg_add(g, v);
    }
}

static px_dec_t expression(void);

/* The arguments of SUM and the like, after the '(': values and ranges. */
static void agg_args(agg_t *g) {
    if (eat(')'))
        return;
    do {
        const char *start = p;
        int r0, c0, r1, c1;
        if (read_range(&r0, &c0, &r1, &c1) && (spaces(), *p == ',' || *p == ')')) {
            if (!skipping)
                agg_range(g, r0, c0, r1, c1);
        } else {
            p = start;
            px_dec_t v = expression();
            if (!skipping)
                agg_add(g, v);
        }
    } while (eat(','));
    expect(')');
}

enum {
    F_SUM, F_AVERAGE, F_MIN, F_MAX, F_COUNT, F_PRODUCT, F_STDEV, F_AND, F_OR, /* many values */
    F_IF,
    F_ABS, F_SQRT, F_EXP, F_LN, F_LOG, F_LOG10,
    F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_ATAN2, F_SINH, F_COSH, F_TANH,
    F_PI, F_RADIANS, F_DEGREES,
    F_INT, F_TRUNC, F_ROUND, F_MOD, F_POWER, F_FACT, F_GAMMA, F_SIGN, F_NOT, F_RAND,
    F_COUNT_ALL
};

const sheet_function_t sheet_functions[] = {
    {"SUM", "values", "Total"},
    {"AVERAGE", "values", "Mean"},
    {"MIN", "values", "Smallest"},
    {"MAX", "values", "Largest"},
    {"COUNT", "values", "How many numbers"},
    {"PRODUCT", "values", "All multiplied"},
    {"STDEV", "values", "Standard deviation"},
    {"AND", "values", "1 if none is 0"},
    {"OR", "values", "1 if any isn't 0"},
    {"IF", "test,yes,no", "yes if test isn't 0"},
    {"ABS", "x", "Without its sign"},
    {"SQRT", "x", "Square root"},
    {"EXP", "x", "e to the x"},
    {"LN", "x", "Natural logarithm"},
    {"LOG", "x,base", "Logarithm, base 10"},
    {"LOG10", "x", "Logarithm, base 10"},
    {"SIN", "x", "Sine, x in radians"},
    {"COS", "x", "Cosine, x in radians"},
    {"TAN", "x", "Tangent, x in radians"},
    {"ASIN", "x", "Arc sine"},
    {"ACOS", "x", "Arc cosine"},
    {"ATAN", "x", "Arc tangent"},
    {"ATAN2", "x,y", "Angle of point x,y"},
    {"SINH", "x", "Hyperbolic sine"},
    {"COSH", "x", "Hyperbolic cosine"},
    {"TANH", "x", "Hyperbolic tangent"},
    {"PI", "", "3.14159..."},
    {"RADIANS", "deg", "Degrees to radians"},
    {"DEGREES", "rad", "Radians to degrees"},
    {"INT", "x", "Down to a whole number"},
    {"TRUNC", "x,digits", "Cut after digits"},
    {"ROUND", "x,digits", "Round to digits"},
    {"MOD", "a,b", "Remainder, b's sign"},
    {"POWER", "a,b", "a to the power b"},
    {"FACT", "n", "n! = 1x2x...xn"},
    {"GAMMA", "x", "Gamma, (x-1)!"},
    {"SIGN", "x", "-1, 0 or 1"},
    {"NOT", "x", "1 if x is 0"},
    {"RAND", "", "Random, 0 to 1"},
};
const int sheet_function_count = F_COUNT_ALL;
_Static_assert(sizeof(sheet_functions) / sizeof(sheet_functions[0]) == F_COUNT_ALL, "one entry a function");

/* The fewest and most arguments of each function from F_IF on. */
static const int8_t args_min[F_COUNT_ALL] = {
    [F_IF] = 2, [F_ABS] = 1, [F_SQRT] = 1, [F_EXP] = 1, [F_LN] = 1, [F_LOG] = 1, [F_LOG10] = 1,
    [F_SIN] = 1, [F_COS] = 1, [F_TAN] = 1, [F_ASIN] = 1, [F_ACOS] = 1, [F_ATAN] = 1, [F_ATAN2] = 2,
    [F_SINH] = 1, [F_COSH] = 1, [F_TANH] = 1, [F_PI] = 0, [F_RADIANS] = 1, [F_DEGREES] = 1,
    [F_INT] = 1, [F_TRUNC] = 1, [F_ROUND] = 1, [F_MOD] = 2, [F_POWER] = 2, [F_FACT] = 1,
    [F_GAMMA] = 1, [F_SIGN] = 1, [F_NOT] = 1, [F_RAND] = 0,
};
static const int8_t args_max[F_COUNT_ALL] = {
    [F_IF] = 3, [F_ABS] = 1, [F_SQRT] = 1, [F_EXP] = 1, [F_LN] = 1, [F_LOG] = 2, [F_LOG10] = 1,
    [F_SIN] = 1, [F_COS] = 1, [F_TAN] = 1, [F_ASIN] = 1, [F_ACOS] = 1, [F_ATAN] = 1, [F_ATAN2] = 2,
    [F_SINH] = 1, [F_COSH] = 1, [F_TANH] = 1, [F_PI] = 0, [F_RADIANS] = 1, [F_DEGREES] = 1,
    [F_INT] = 1, [F_TRUNC] = 2, [F_ROUND] = 2, [F_MOD] = 2, [F_POWER] = 2, [F_FACT] = 1,
    [F_GAMMA] = 1, [F_SIGN] = 1, [F_NOT] = 1, [F_RAND] = 0,
};

static px_dec_t pi(void) {
    px_dec_t r;
    px->dec_from_string(&r, "3.141592653589793238462643383279503");
    return r;
}

/* 10^n, exactly. */
static px_dec_t ten_to(px_dec_t n) {
    int32_t k = 0;
    px->dec_to_int(&k, &n);
    if (k > 100 || k < -100)
        fail(SHEET_NUM);
    return op2(PX_DEC_POW, from_int(10), from_int(k));
}

static px_dec_t aggregate(int f, const agg_t *g) {
    switch (f) {
        case F_SUM: return g->sum;
        case F_AVERAGE:
            if (g->n == 0)
                fail(SHEET_DIV0);
            return divide(g->sum, from_int(g->n));
        case F_MIN: return g->n ? g->min : from_int(0);
        case F_MAX: return g->n ? g->max : from_int(0);
        case F_COUNT: return from_int(g->n);
        case F_PRODUCT: return g->n ? g->prod : from_int(0);
        case F_STDEV: {
            if (g->n < 2)
                fail(SHEET_DIV0);
            px_dec_t n = from_int(g->n);
            px_dec_t var = divide(op2(PX_DEC_SUB, g->sumsq, divide(op2(PX_DEC_MUL, g->sum, g->sum), n)),
                                  from_int(g->n - 1));
            px_dec_t zero = from_int(0);
            if (px->dec_cmp(&var, &zero) < 0)
                var = zero; /* -1E-33 from rounding */
            return op1(PX_DEC_SQRT, var);
        }
        case F_AND:
            if (g->n == 0)
                fail(SHEET_VALUE);
            return from_int(g->all);
        default:
            if (g->n == 0)
                fail(SHEET_VALUE);
            return from_int(g->any);
    }
}

static px_dec_t function(int f) {
    if (f <= F_OR) {
        agg_t g = {.all = true};
        g.sum = g.sumsq = from_int(0);
        g.prod = from_int(1);
        agg_args(&g);
        return skipping ? from_int(0) : aggregate(f, &g);
    }
    if (f == F_IF) {
        px_dec_t test = expression(), r = from_int(0);
        expect(',');
        bool first = skipping || !is_zero(&test);
        if (!first)
            skipping++;
        px_dec_t yes = expression();
        if (!first)
            skipping--;
        else
            r = yes;
        if (eat(',')) {
            if (first)
                skipping++;
            px_dec_t no = expression();
            if (first)
                skipping--;
            else
                r = no;
        }
        expect(')');
        return r;
    }

    px_dec_t a[2] = {from_int(0), from_int(0)};
    int n = 0;
    if (!eat(')')) {
        do {
            px_dec_t v = expression();
            if (n < 2)
                a[n] = v;
            n++;
        } while (eat(','));
        expect(')');
    }
    if (n < args_min[f] || n > args_max[f])
        fail(SHEET_SYNTAX);
    if (skipping)
        return from_int(0);

    px_dec_t x = a[0], zero = from_int(0);
    switch (f) {
        case F_ABS: return op1(PX_DEC_ABS, x);
        case F_SQRT: return op1(PX_DEC_SQRT, x);
        case F_EXP: return op1(PX_DEC_EXP, x);
        case F_LN: return op1(PX_DEC_LN, x);
        case F_LOG:
            if (n == 1)
                return op1(PX_DEC_LOG10, x);
            return divide(op1(PX_DEC_LN, x), op1(PX_DEC_LN, a[1]));
        case F_LOG10: return op1(PX_DEC_LOG10, x);
        case F_SIN: return op1(PX_DEC_SIN, x);
        case F_COS: return op1(PX_DEC_COS, x);
        case F_TAN: return op1(PX_DEC_TAN, x);
        case F_ASIN: return op1(PX_DEC_ASIN, x);
        case F_ACOS: return op1(PX_DEC_ACOS, x);
        case F_ATAN: return op1(PX_DEC_ATAN, x);
        case F_ATAN2: /* Excel's ATAN2(x, y): the angle of the point (x, y) */
            if (is_zero(&a[0]) && is_zero(&a[1]))
                fail(SHEET_DIV0);
            return op2(PX_DEC_ATAN2, a[1], a[0]);
        case F_SINH: return op1(PX_DEC_SINH, x);
        case F_COSH: return op1(PX_DEC_COSH, x);
        case F_TANH: return op1(PX_DEC_TANH, x);
        case F_PI: return pi();
        case F_RADIANS: return divide(op2(PX_DEC_MUL, x, pi()), from_int(180));
        case F_DEGREES: return divide(op2(PX_DEC_MUL, x, from_int(180)), pi());
        case F_INT: return op1(PX_DEC_FLOOR, x);
        case F_TRUNC:
        case F_ROUND: {
            px_dec_t scale = ten_to(n > 1 ? a[1] : zero), s = op2(PX_DEC_MUL, x, scale);
            if (f == F_TRUNC)
                return divide(op1(PX_DEC_TRUNC, s), scale);
            /* half away from 0 */
            px_dec_t half, r;
            px->dec_from_string(&half, "0.5");
            r = op1(PX_DEC_FLOOR, op2(PX_DEC_ADD, op1(PX_DEC_ABS, s), half));
            if (px->dec_cmp(&s, &zero) < 0)
                r = op1(PX_DEC_NEG, r);
            return divide(r, scale);
        }
        case F_MOD: /* a - b * INT(a / b): b's sign, as Excel */
            return op2(PX_DEC_SUB, x, op2(PX_DEC_MUL, a[1], op1(PX_DEC_FLOOR, divide(x, a[1]))));
        case F_POWER: return power(x, a[1]);
        case F_FACT: {
            int32_t k;
            if (px->dec_cmp(&x, &zero) < 0 || px->dec_to_int(&k, &x) != 0 || k > 3300)
                fail(SHEET_NUM);
            px_dec_t r = from_int(1);
            for (int i = 2; i <= k; i++)
                r = op2(PX_DEC_MUL, r, from_int(i));
            return r;
        }
        case F_GAMMA: return op1(PX_DEC_GAMMA, x);
        case F_SIGN: return from_int(px->dec_cmp(&x, &zero));
        case F_NOT: return from_int(is_zero(&x));
        default: /* F_RAND */
            return divide(from_int((int) (px->random() % 1000000000u)), from_int(1000000000));
    }
}

static px_dec_t primary(void) {
    spaces();
    if (++depth > DEPTH_MAX)
        fail(SHEET_SYNTAX);
    px_dec_t v = from_int(0);
    if (*p == '(') {
        p++;
        v = expression();
        expect(')');
    } else if (isdigit((unsigned char) *p) || (*p == '.' && isdigit((unsigned char) p[1]))) {
        char buf[64];
        int n = 0;
        while ((isdigit((unsigned char) *p) || *p == '.') && n < 60)
            buf[n++] = *p++;
        if ((*p == 'E' || *p == 'e')
                && (isdigit((unsigned char) p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char) p[2])))) {
            buf[n++] = 'E';
            p++;
            if (*p == '+' || *p == '-')
                buf[n++] = *p++;
            while (isdigit((unsigned char) *p) && n < 62)
                buf[n++] = *p++;
        }
        buf[n] = 0;
        if (px->dec_from_string(&v, buf) != 0)
            fail(SHEET_SYNTAX);
    } else if (strncmp(p, "#REF!", 5) == 0) {
        p += 5;
        if (!skipping)
            fail(SHEET_REF);
    } else if (isalpha((unsigned char) *p) || *p == '$') {
        const char *start = p;
        int r0, c0, r1, c1;
        if (read_range(&r0, &c0, &r1, &c1)) {
            if ((r0 != r1 || c0 != c1) && !skipping) /* a range alone */
                fail(SHEET_VALUE);
            if (!skipping && cell_get(r0 * SHEET_COLS + c0, &v) == 2)
                fail(SHEET_VALUE);
        } else {
            char name[12];
            int n = 0;
            p = start;
            while (isalnum((unsigned char) *p) || *p == '_' || *p == '.') {
                if (n < 11)
                    name[n++] = (char) toupper((unsigned char) *p);
                p++;
            }
            name[n] = 0;
            if (n == 0)
                fail(SHEET_SYNTAX);
            if (eat('(')) {
                int f = 0;
                while (f < F_COUNT_ALL && strcmp(name, sheet_functions[f].name) != 0)
                    f++;
                if (f < F_COUNT_ALL) {
                    v = function(f);
                } else {
                    if (!skipping)
                        fail(SHEET_NAME);
                    if (!eat(')')) { /* read what it was given, for sheet_check() */
                        do
                            expression();
                        while (eat(','));
                        expect(')');
                    }
                }
            } else if (strcmp(name, "TRUE") == 0) {
                v = from_int(1);
            } else if (strcmp(name, "FALSE") != 0 && !skipping) {
                fail(SHEET_NAME);
            }
        }
    } else {
        fail(SHEET_SYNTAX);
    }
    depth--;
    return v;
}

static px_dec_t postfix(void) {
    px_dec_t v = primary();
    while (eat('%'))
        v = divide(v, from_int(100));
    return v;
}

static px_dec_t unary(void) {
    if (eat('-'))
        return op1(PX_DEC_NEG, unary());
    if (eat('+'))
        return unary();
    return postfix();
}

static px_dec_t powers(void) {
    px_dec_t v = unary();
    while (eat('^'))
        v = power(v, unary());
    return v;
}

static px_dec_t product(void) {
    px_dec_t v = powers();
    for (;;) {
        if (eat('*'))
            v = op2(PX_DEC_MUL, v, powers());
        else if (eat('/'))
            v = divide(v, powers());
        else
            return v;
    }
}

static px_dec_t sum(void) {
    px_dec_t v = product();
    for (;;) {
        if (eat('+'))
            v = op2(PX_DEC_ADD, v, product());
        else if (eat('-'))
            v = op2(PX_DEC_SUB, v, product());
        else
            return v;
    }
}

static px_dec_t expression(void) {
    px_dec_t v = sum();
    for (;;) {
        spaces();
        int lt = 0, eq = 0, gt = 0; /* which outcomes of the comparison are true */
        if (p[0] == '<' && p[1] == '=') {
            lt = eq = 1;
            p += 2;
        } else if (p[0] == '>' && p[1] == '=') {
            gt = eq = 1;
            p += 2;
        } else if (p[0] == '<' && p[1] == '>') {
            lt = gt = 1;
            p += 2;
        } else if (*p == '<') {
            lt = 1;
            p++;
        } else if (*p == '>') {
            gt = 1;
            p++;
        } else if (*p == '=') {
            eq = 1;
            p++;
        } else {
            return v;
        }
        px_dec_t w = sum();
        if (!skipping) {
            int c = px->dec_cmp(&v, &w);
            v = from_int(c < 0 ? lt : c == 0 ? eq : c == 1 ? gt : 0);
        }
    }
}

/* formula ("=..."): SHEET_OK and *v, an error, or BLOCKED. */
static int evaluate(const char *formula, px_dec_t *v) {
    int err = setjmp(jump);
    if (err != 0) {
        skipping = 0;
        return err;
    }
    p = formula + 1;
    depth = 0;
    px_dec_t r = expression();
    spaces();
    if (*p != 0)
        fail(SHEET_SYNTAX);
    *v = r;
    return SHEET_OK;
}

int sheet_check(const char *formula) {
    px_dec_t v;
    skipping = 1;
    int err = evaluate(formula, &v);
    skipping = 0;
    return err == SHEET_OK ? -1 : (int) (p - formula);
}

void sheet_recalc(void) {
    for (int i = 0; i < ncells; i++)
        cells[i].pending = cells[i].kind == CELL_FORMULA;
    for (bool progress = true; progress;) {
        progress = false;
        for (int i = 0; i < ncells; i++) {
            cell_t *c = &cells[i];
            if (!c->pending)
                continue;
            px_dec_t v;
            int err = evaluate(c->text, &v);
            if (err == BLOCKED)
                continue;
            c->pending = false;
            c->err = (uint8_t) err;
            c->num = err == SHEET_OK ? v : from_int(0);
            progress = true;
        }
    }
    for (int i = 0; i < ncells; i++)
        if (cells[i].pending) {
            cells[i].pending = false;
            cells[i].err = SHEET_CIRC;
        }
}

/* ===================== showing a value ===================== */

/* A number as digits: d[0..n-1] (none in front or at the end is 0, n = 0
 * for zero) times 10^q. */
typedef struct {
    bool neg;
    int n, q;
    char d[40];
} digits_t;

static void strip(digits_t *g) {
    while (g->n > 0 && g->d[g->n - 1] == '0') {
        g->n--;
        g->q++;
    }
}

static void to_digits(const px_dec_t *v, digits_t *g) {
    char s[PX_DEC_STRING_MAX];
    px->dec_to_string(s, v); /* "+31415E-4": sign, digits, exponent */
    const char *e = strchr(s, 'E');
    g->neg = s[0] == '-';
    g->n = 0;
    g->q = e != NULL ? atoi(e + 1) : 0;
    for (const char *c = s + 1; e != NULL && c < e; c++)
        if ((g->n > 0 || *c != '0') && g->n < (int) sizeof(g->d))
            g->d[g->n++] = *c;
    strip(g);
}

/* Keeps the first k digits (k may be 0 or less), rounded half up. */
static void keep(digits_t *g, int k) {
    if (k >= g->n)
        return;
    if (k < 0) {
        g->n = 0;
        return;
    }
    bool up = g->d[k] >= '5';
    g->q += g->n - k;
    g->n = k;
    for (int i = k - 1; up && i >= 0; i--) {
        up = g->d[i] == '9';
        g->d[i] = up ? '0' : (char) (g->d[i] + 1);
    }
    if (up) { /* 999 -> 1000: still n digits, one place up */
        if (g->n > 0)
            memmove(g->d + 1, g->d, (size_t) g->n - 1);
        else
            g->n = 1;
        g->d[0] = '1';
        g->q++;
    }
    strip(g);
}

static int top(const digits_t *g) { /* the first digit's power of ten */
    return g->q + g->n - 1;
}

static char digit(const digits_t *g, int power) {
    int i = top(g) - power;
    return i >= 0 && i < g->n ? g->d[i] : '0';
}

/* Plain, with `decimals` after the point: its length, or 0 if it takes
 * more than width. */
static int plain(char *out, const digits_t *g, int decimals, int width) {
    int whole = g->n > 0 && top(g) >= 0 ? top(g) + 1 : 1;
    int len = (g->neg && g->n > 0) + whole + (decimals > 0 ? decimals + 1 : 0);
    if (len > width)
        return 0;
    int k = 0;
    if (g->neg && g->n > 0)
        out[k++] = '-';
    for (int power = whole - 1; power >= 0; power--)
        out[k++] = digit(g, power);
    if (decimals > 0) {
        out[k++] = '.';
        for (int power = -1; power >= -decimals; power--)
            out[k++] = digit(g, power);
    }
    out[k] = 0;
    return k;
}

/* d.dddE-7 with `decimals` digits after the point: its length, or 0. */
static int scientific(char *out, const digits_t *g, int decimals, int width) {
    char e[8];
    int elen = snprintf(e, sizeof(e), "E%d", g->n > 0 ? top(g) : 0);
    int len = (g->neg && g->n > 0) + 1 + (decimals > 0 ? decimals + 1 : 0) + elen;
    if (len > width)
        return 0;
    int k = 0;
    if (g->neg && g->n > 0)
        out[k++] = '-';
    out[k++] = g->n > 0 ? g->d[0] : '0';
    if (decimals > 0) {
        out[k++] = '.';
        for (int i = 1; i <= decimals; i++)
            out[k++] = i < g->n ? g->d[i] : '0';
    }
    strcpy(out + k, e);
    return k + elen;
}

#define GENERAL_DIGITS 15 /* as Excel shows them */

static bool format_number(char *buf, int width, const px_dec_t *v, sheet_fmt_t fmt) {
    digits_t g;
    to_digits(v, &g);
    if (fmt.mode == FMT_FIX) {
        keep(&g, g.n + g.q + fmt.digits); /* the digits down to 10^-digits */
        return plain(buf, &g, fmt.digits, width) > 0;
    }
    if (fmt.mode == FMT_SCI) {
        keep(&g, fmt.digits + 1);
        return scientific(buf, &g, fmt.digits, width) > 0;
    }
    if (g.n == 0)
        return plain(buf, &g, 0, width) > 0;
    keep(&g, GENERAL_DIGITS);
    /* Plain with what decimals fit, unless that loses it all or it's
     * far from 1; else d.dddE7 with what digits fit. */
    if (top(&g) >= -5 && top(&g) < GENERAL_DIGITS) {
        int whole = top(&g) >= 0 ? top(&g) + 1 : 1;
        int room = width - g.neg - whole - 1;
        digits_t h = g;
        if (-h.q > room)
            keep(&h, h.n + h.q + (room > 0 ? room : 0));
        if (h.n > 0 && plain(buf, &h, h.q < 0 ? -h.q : 0, width) > 0)
            return true;
    }
    for (int decimals = g.n - 1; decimals >= 0; decimals--) {
        digits_t h = g;
        keep(&h, decimals + 1);
        if (scientific(buf, &h, h.n > 1 ? h.n - 1 : 0, width) > 0)
            return true;
    }
    return false;
}

const char *sheet_error_text(sheet_err_t err) {
    static const char *const text[] = {"", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#CIRC!", "#SYNTAX"};
    return text[err];
}

void sheet_format(char *buf, int width, const sheet_value_t *v, sheet_fmt_t fmt, bool *right) {
    *right = false;
    buf[0] = 0;
    const char *s = "";
    if (v->kind == CELL_TEXT) {
        s = v->text;
    } else if (v->kind == CELL_FORMULA && v->err != SHEET_OK) {
        s = sheet_error_text(v->err);
    } else if (v->kind != CELL_EMPTY) {
        *right = true;
        if (!format_number(buf, width, &v->num, fmt)) {
            memset(buf, '#', (size_t) width);
            buf[width] = 0;
        }
        return;
    }
    snprintf(buf, (size_t) width + 1, "%s", s);
}

/* ===================== copying ===================== */

bool sheet_shift(const char *in, int drow, int dcol, char *out, int size) {
    int k = 0;
#define PUT(c)                \
    do {                      \
        if (k >= size - 1)    \
            return false;     \
        out[k++] = (char) (c); \
    } while (0)
    if (in[0] != '=') {
        if ((int) strlen(in) >= size)
            return false;
        strcpy(out, in);
        return true;
    }
    const char *s = in;
    while (*s) {
        int row, col, abs;
        int n = isalpha((unsigned char) *s) || *s == '$' ? ref_at(s, &row, &col, &abs) : 0;
        if (n > 0 && col >= 0) {
            if (!(abs & ABS_COL))
                col += dcol;
            if (!(abs & ABS_ROW))
                row += drow;
            char name[12];
            if (col < 0 || col >= SHEET_COLS || row < 0 || row >= SHEET_ROWS)
                strcpy(name, "#REF!");
            else
                snprintf(name, sizeof(name), "%s%c%s%d", abs & ABS_COL ? "$" : "", 'A' + col, abs & ABS_ROW ? "$" : "", row + 1);
            for (const char *c = name; *c; c++)
                PUT(*c);
            s += n;
        } else if (n > 0 || isalnum((unsigned char) *s) || *s == '$' || *s == '_') {
            /* a word that isn't a name (a function, AA1), or a number: as it is */
            if (n == 0)
                n = 1;
            const char *end = s + n;
            while (isalnum((unsigned char) *end) || *end == '_' || *end == '$' || *end == '.')
                end++;
            if (isdigit((unsigned char) *s) && (end[-1] == 'E' || end[-1] == 'e') && (*end == '+' || *end == '-'))
                for (end++; isdigit((unsigned char) *end); end++) /* 1E-5 */
                    ;
            for (; s < end; s++)
                PUT(*s);
        } else {
            PUT(*s++);
        }
    }
    out[k] = 0;
    return true;
#undef PUT
}

/* ===================== CSV ===================== */

char sheet_csv_guess_sep(const char *data, int n) {
    int count[3] = {0, 0, 0}; /* , ; tab, outside quotes, on the first line */
    bool quoted = false;
    for (int i = 0; i < n && (quoted || data[i] != '\n'); i++) {
        if (data[i] == '"')
            quoted = !quoted;
        else if (!quoted)
            count[0] += data[i] == ',', count[1] += data[i] == ';', count[2] += data[i] == '\t';
    }
    return count[1] > count[0] && count[1] >= count[2] ? ';' : count[2] > count[0] ? '\t' : ',';
}

enum { CSV_START, CSV_PLAIN, CSV_QUOTED, CSV_QUOTE };

static struct {
    char sep;
    int state, row, col, len, at;
    bool lost, full;
    char field[SHEET_INPUT_MAX + 1];
} csv;

void sheet_csv_begin(char sep) {
    memset(&csv, 0, sizeof(csv));
    csv.sep = sep;
}

static void csv_field_end(void) {
    csv.field[csv.len] = 0;
    if (csv.len > 0) {
        if (csv.row >= SHEET_ROWS || csv.col >= SHEET_COLS) {
            csv.lost = true;
        } else {
            /* "1,5" in a ';' file: 1.5 */
            char *comma = csv.sep == ';' ? strchr(csv.field, ',') : NULL;
            px_dec_t v;
            if (comma != NULL && strchr(comma + 1, ',') == NULL && strchr(csv.field, '.') == NULL) {
                *comma = '.';
                if (!parse_number(csv.field, &v))
                    *comma = ',';
            }
            if (!sheet_set(csv.row, csv.col, csv.field))
                csv.full = true;
        }
    }
    csv.col++;
    csv.len = 0;
    csv.state = CSV_START;
}

static void csv_char(char c) {
    if (csv.len < SHEET_INPUT_MAX)
        csv.field[csv.len++] = c;
    else
        csv.lost = true;
}

void sheet_csv_feed(const char *data, int n) {
    for (int i = 0; i < n; i++, csv.at++) {
        char c = data[i];
        if (csv.at < 3 && (unsigned char) c == (unsigned char) "\xEF\xBB\xBF"[csv.at] && csv.row == 0 && csv.col == 0 && csv.len == 0)
            continue; /* UTF-8's mark at the start */
        switch (csv.state) {
            case CSV_QUOTED:
                if (c == '"')
                    csv.state = CSV_QUOTE;
                else
                    csv_char(c);
                continue;
            case CSV_QUOTE:
                if (c == '"') {
                    csv_char(c);
                    csv.state = CSV_QUOTED;
                    continue;
                }
                csv.state = CSV_PLAIN;
                break;
            case CSV_START:
                if (c == '"') {
                    csv.state = CSV_QUOTED;
                    continue;
                }
                csv.state = CSV_PLAIN;
                break;
        }
        if (c == csv.sep) {
            csv_field_end();
        } else if (c == '\n') {
            csv_field_end();
            csv.row++;
            csv.col = 0;
        } else if (c != '\r') {
            csv_char(c);
        }
    }
}

bool sheet_csv_end(bool *lost) {
    if (csv.state != CSV_START || csv.len > 0)
        csv_field_end();
    *lost = csv.lost;
    return !csv.full;
}

bool sheet_csv_write(bool (*out)(void *ctx, const char *s, int n), void *ctx) {
    int last_row, last_col, i = 0;
    sheet_extent(&last_row, &last_col);
    for (int row = 0; row <= last_row; row++) {
        for (int col = 0; col <= last_col; col++) {
            if (col > 0 && !out(ctx, ",", 1))
                return false;
            const char *t = i < ncells && cells[i].pos == row * SHEET_COLS + col ? cells[i++].text : "";
            if (strpbrk(t, ",\"\r\n") == NULL && t[0] != ' ') {
                if (!out(ctx, t, (int) strlen(t)))
                    return false;
                continue;
            }
            if (!out(ctx, "\"", 1))
                return false;
            for (const char *c = t; *c; c++)
                if ((*c == '"' && !out(ctx, "\"", 1)) || !out(ctx, c, 1))
                    return false;
            if (!out(ctx, "\"", 1))
                return false;
        }
        if (!out(ctx, "\r\n", 2))
            return false;
    }
    return true;
}
