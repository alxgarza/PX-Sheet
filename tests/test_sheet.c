/* PX-Sheet's engine (src/sheet.c) on a computer (make test): cells
 * set as typed, then what they show compared with what they should. The
 * numbers are the simulator's (sim/sim_dec.c): doubles, so the expected
 * results are ones that show the same at 15 digits as the calculator's
 * decimals. */
#include <stdio.h>
#include <string.h>

#include "px.h"
#include "sheet.h"
#include "sim_dec.h"

static uint32_t fixed_random(void) {
    return 123456789;
}

static const px_api_t api = {
        .random = fixed_random,
        .dec = sim_dec_op,
        .dec_from_int = sim_dec_from_int,
        .dec_to_int = sim_dec_to_int,
        .dec_from_string = sim_dec_from_string,
        .dec_to_string = sim_dec_to_string,
        .dec_cmp = sim_dec_cmp,
        .dec_class = sim_dec_class,
};
const px_api_t *px = &api;

static int failures;

static void at(const char *name, int *row, int *col) {
    if (sheet_parse_ref(name, row, col) == 0)
        printf("bad cell name %s\n", name);
}

static void set(const char *name, const char *input) {
    int row, col;
    at(name, &row, &col);
    if (!sheet_set(row, col, input))
        printf("no room for %s\n", name);
}

static const char *show(const char *name, int width, sheet_fmt_t fmt) {
    static char buf[64];
    int row, col;
    bool right;
    sheet_value_t v;
    at(name, &row, &col);
    sheet_value(row, col, &v);
    sheet_format(buf, width, &v, fmt, &right);
    return buf;
}

static const sheet_fmt_t general = {FMT_GENERAL, 0};

static void expect_shown(int line, const char *name, int width, sheet_fmt_t fmt, const char *want) {
    const char *got = show(name, width, fmt);
    if (strcmp(got, want) != 0) {
        printf("line %d: %s (%s) shows \"%s\", not \"%s\"\n", line, name, sheet_input(0, 0), got, want);
        failures++;
    }
}

/* A formula in Z1, worked out: what it shows, 20 wide. */
static void expect_formula(int line, const char *formula, const char *want) {
    set("Z1", formula);
    sheet_recalc();
    const char *got = show("Z1", 20, general);
    if (strcmp(got, want) != 0) {
        printf("line %d: %s shows \"%s\", not \"%s\"\n", line, formula, got, want);
        failures++;
    }
}

#define SHOWS(name, want)            expect_shown(__LINE__, name, 20, general, want)
#define SHOWS_AS(name, w, fmt, want) expect_shown(__LINE__, name, w, fmt, want)
#define GIVES(formula, want)         expect_formula(__LINE__, formula, want)
#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            printf("line %d: not true: %s\n", __LINE__, #cond); \
            failures++;                                          \
        }                                                        \
    } while (0)

static char written[4096];
static int written_len;

static bool out(void *ctx, const char *s, int n) {
    (void) ctx;
    if (written_len + n >= (int) sizeof(written))
        return false;
    memcpy(written + written_len, s, (size_t) n);
    written_len += n;
    written[written_len] = 0;
    return true;
}

static void load(const char *text, int piece) {
    sheet_clear();
    int n = (int) strlen(text);
    sheet_csv_begin(sheet_csv_guess_sep(text, n));
    for (int i = 0; i < n; i += piece)
        sheet_csv_feed(text + i, n - i < piece ? n - i : piece);
    bool lost;
    sheet_csv_end(&lost);
    sheet_recalc();
}

int main(void) {
    /* What's typed: numbers, text, formulas. */
    set("A1", "1");
    set("A2", " 2 ");
    set("A3", "3e0");
    set("A4", "total");
    set("A5", "'123");
    set("A6", "12abc");
    sheet_recalc();
    SHOWS("A1", "1");
    SHOWS("A2", "2");
    SHOWS("A3", "3");
    SHOWS("A4", "total");
    SHOWS("A5", "123");
    SHOWS("A6", "12abc");

    /* Ranges skip text, as in Excel. */
    GIVES("=SUM(A1:A6)", "6");
    GIVES("=sum(a1:a3)", "6");
    GIVES("=AVERAGE(A1:A4)", "2");
    GIVES("=MIN(A3:A1)", "1");
    GIVES("=MAX(A1:A3, 10)", "10");
    GIVES("=COUNT(A1:A6)", "3");
    GIVES("=PRODUCT(A1:A3)", "6");
    GIVES("=STDEV(A1:A3)", "1");
    GIVES("=SUM(A1, A3)", "4");
    GIVES("=SUM(B1:B9)", "0");

    /* Excel's precedence. */
    GIVES("=1+2*3", "7");
    GIVES("=(1+2)*3", "9");
    GIVES("=-2^2", "4");
    GIVES("=2^3^2", "64");
    GIVES("=2^-1", "0.5");
    GIVES("=10%", "0.1");
    GIVES("=0.1+0.2", "0.3");
    GIVES("=1<2", "1");
    GIVES("=2<>2", "0");
    GIVES("=3>=3", "1");
    GIVES("=1+1=2", "1");
    GIVES("= 1 + 2 ", "3");

    /* Errors. */
    GIVES("=1/0", "#DIV/0!");
    GIVES("=SQRT(-1)", "#NUM!");
    GIVES("=0^0", "#NUM!");
    GIVES("=A1+A4", "#VALUE!");
    GIVES("=A1:A3", "#VALUE!");
    GIVES("=FOO(1)", "#NAME?");
    GIVES("=AA1", "#REF!");
    GIVES("=A1000", "#REF!");
    GIVES("=1+", "#SYNTAX");
    GIVES("=SIN(1,2)", "#SYNTAX");
    GIVES("=AVERAGE(B1:B3)", "#DIV/0!");
    CHECK(sheet_check("=SUM(A1:A3)") == -1);
    CHECK(sheet_check("=FOO(1)") == -1); /* a name: #NAME?, not unreadable */
    CHECK(sheet_check("=SUM(A1") == 7);
    CHECK(sheet_check("=1+*2") == 3);
    CHECK(sheet_check("=") == 1);

    /* IF only works out the branch it takes. */
    GIVES("=IF(A1>0, 10, 1/0)", "10");
    GIVES("=IF(0, 1/0, 5)", "5");
    GIVES("=IF(0, 1)", "0");
    GIVES("=IF(1)", "#SYNTAX");

    /* The functions. */
    GIVES("=ROUND(2.345, 2)", "2.35");
    GIVES("=ROUND(-2.5)", "-3");
    GIVES("=ROUND(1234.5, -2)", "1200");
    GIVES("=TRUNC(-2.7)", "-2");
    GIVES("=INT(-2.5)", "-3");
    GIVES("=MOD(-7, 3)", "2");
    GIVES("=MOD(7, -3)", "-2");
    GIVES("=MOD(1, 0)", "#DIV/0!");
    GIVES("=FACT(5)", "120");
    GIVES("=FACT(0)", "1");
    GIVES("=FACT(-1)", "#NUM!");
    GIVES("=GAMMA(5)", "24");
    GIVES("=PI()", "3.14159265358979");
    GIVES("=DEGREES(PI())", "180");
    GIVES("=SIN(RADIANS(30))", "0.5");
    GIVES("=LOG(1000)", "3");
    GIVES("=LOG(8, 2)", "3");
    GIVES("=LN(EXP(2))", "2");
    GIVES("=ATAN2(1, 1)*4", "3.14159265358979");
    GIVES("=ATAN2(0, 0)", "#DIV/0!");
    GIVES("=ABS(-3)", "3");
    GIVES("=SIGN(-2)", "-1");
    GIVES("=POWER(2, 10)", "1024");
    GIVES("=NOT(0)", "1");
    GIVES("=AND(1, 0)", "0");
    GIVES("=OR(0, 0, 1)", "1");
    GIVES("=TRUE+TRUE", "2");
    GIVES("=RAND()", "0.123456789");
    GIVES("=LOG10(100)+A1", "3");

    /* Formulas need each other, in any order. */
    sheet_clear();
    set("A10", "=A11+1");
    set("A11", "=A12+1");
    set("A12", "5");
    for (int r = 0; r < 500; r++) { /* a chain up the sheet: 500 passes, no deep recursion */
        char input[16];
        if (r == 499)
            strcpy(input, "0");
        else
            snprintf(input, sizeof(input), "=B%d+1", r + 2);
        sheet_set(r, 1, input);
    }
    set("C1", "=C2");
    set("C2", "=C1+1");
    set("C3", "=C3");
    set("C4", "=SUM(C4:C5)");
    set("D1", "=1/0");
    set("D2", "=D1+1");
    set("D3", "=C1");
    sheet_recalc();
    SHOWS("A10", "7");
    SHOWS("B1", "499");
    SHOWS("C1", "#CIRC!");
    SHOWS("C2", "#CIRC!");
    SHOWS("C3", "#CIRC!");
    SHOWS("C4", "#CIRC!");
    SHOWS("D2", "#DIV/0!");
    SHOWS("D3", "#CIRC!");

    /* How numbers are shown. */
    sheet_clear();
    set("A1", "3.14159");
    set("A2", "123456");
    set("A3", "2.5");
    set("A4", "-0.001");
    set("A5", "12345");
    set("A6", "0.000001");
    set("A7", "0.0001");
    set("A8", "1E20");
    set("A9", "-1.5");
    set("A10", "0");
    set("A11", "9.999");
    sheet_recalc();
    SHOWS_AS("A1", 6, general, "3.1416");
    SHOWS_AS("A1", 1, general, "3");
    SHOWS_AS("A2", 6, general, "123456");
    SHOWS_AS("A2", 5, general, "1.2E5");
    SHOWS_AS("A2", 3, general, "1E5");
    SHOWS_AS("A2", 2, general, "##");
    SHOWS_AS("A3", 9, ((sheet_fmt_t){FMT_FIX, 2}), "2.50");
    SHOWS_AS("A4", 9, ((sheet_fmt_t){FMT_FIX, 2}), "0.00");
    SHOWS_AS("A4", 9, general, "-0.001");
    SHOWS_AS("A5", 9, ((sheet_fmt_t){FMT_SCI, 2}), "1.23E4");
    SHOWS_AS("A5", 4, ((sheet_fmt_t){FMT_FIX, 2}), "####");
    SHOWS_AS("A6", 9, general, "1E-6");
    SHOWS_AS("A7", 9, general, "0.0001");
    SHOWS_AS("A8", 9, general, "1E20");
    SHOWS_AS("A9", 9, general, "-1.5");
    SHOWS_AS("A10", 9, general, "0");
    SHOWS_AS("A10", 9, ((sheet_fmt_t){FMT_SCI, 1}), "0.0E0");
    SHOWS_AS("A11", 4, general, "10");
    SHOWS_AS("A11", 9, ((sheet_fmt_t){FMT_FIX, 1}), "10.0");

    /* Copies: relative names move, $ ones don't, off the sheet is #REF!. */
    char shifted[64];
    CHECK(sheet_shift("=A1+$B$1+B$2+$C3", 1, 1, shifted, sizeof(shifted)) && strcmp(shifted, "=B2+$B$1+C$2+$C4") == 0);
    CHECK(sheet_shift("=SUM(A1:A3)*LOG10(A1)", 2, 0, shifted, sizeof(shifted))
          && strcmp(shifted, "=SUM(A3:A5)*LOG10(A3)") == 0);
    CHECK(sheet_shift("=1E-5+A1", 1, 0, shifted, sizeof(shifted)) && strcmp(shifted, "=1E-5+A2") == 0);
    CHECK(sheet_shift("=A1", -1, 0, shifted, sizeof(shifted)) && strcmp(shifted, "=#REF!") == 0);
    CHECK(sheet_shift("A1 text", 5, 5, shifted, sizeof(shifted)) && strcmp(shifted, "A1 text") == 0);
    CHECK(!sheet_shift("=A1+A2", 0, 0, shifted, 4));
    GIVES("=#REF!+1", "#REF!");

    /* CSV out and back. */
    sheet_clear();
    set("A1", "Item");
    set("B1", "Cost, each");
    set("A2", "say \"hi\"");
    set("B2", "2.5");
    set("C3", "=IF(B2>1,B2,0)");
    written_len = 0;
    CHECK(sheet_csv_write(out, NULL));
    CHECK(strcmp(written, "Item,\"Cost, each\",\r\n\"say \"\"hi\"\"\",2.5,\r\n,,\"=IF(B2>1,B2,0)\"\r\n") == 0);
    char copy[sizeof(written)];
    strcpy(copy, written);
    for (int piece = 1; piece <= 7; piece += 6) { /* read whole, and a byte at a time */
        load(copy, piece);
        CHECK(strcmp(sheet_input(0, 1), "Cost, each") == 0);
        CHECK(strcmp(sheet_input(1, 0), "say \"hi\"") == 0);
        CHECK(sheet_count() == 5);
        SHOWS("C3", "2.5");
    }

    /* Excel's other files: a mark in front, ';' with decimal commas, a
     * quoted line end, rows past the sheet. */
    load("\xEF\xBB\xBF" "a;1,5;\"x;y\"\n;2\n", 4);
    SHOWS("A1", "a");
    SHOWS("B1", "1.5");
    SHOWS("C1", "x;y");
    SHOWS("B2", "2");
    load("\"two\nlines\",3\n", 64);
    CHECK(strcmp(sheet_input(0, 0), "two\nlines") == 0);
    SHOWS("B1", "3");
    sheet_clear();
    sheet_csv_begin(',');
    const char *wide = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27\n";
    sheet_csv_feed(wide, (int) strlen(wide));
    bool lost;
    CHECK(sheet_csv_end(&lost) && lost);
    CHECK(sheet_count() == 26);

    if (failures) {
        printf("%d PX-Sheet checks failed\n", failures);
        return 1;
    }
    printf("PX-Sheet: all passed\n");
    return 0;
}
