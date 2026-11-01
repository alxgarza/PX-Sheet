#ifndef SHEET_H
#define SHEET_H

#include <stdbool.h>
#include <stdint.h>

#include "px.h"

/* The spreadsheet itself: cells, formulas, recalculation, how a value is
 * shown, CSV. No screen or keys here (app.c has them), so tests/
 * test_sheet.c runs it on a computer.
 *
 * Cells A1..Z999, kept only where something is typed. What's typed is
 * kept as typed (the CSV writes it back): "=..." is a formula, something
 * that reads as a number a number, anything else text ("'" first: text
 * even if it reads as a number). Formulas are Excel's: + - * / ^ %,
 * = <> < > <= >= (1 or 0), A1 $A$1 A1:B5, SUM(...) and the rest of
 * sheet_functions[] -- a CSV opens in Excel or Numbers with its formulas.
 * As in Excel, -2^2 is 4 and 2^3^2 is 64. The numbers are the calculator's
 * 34-digit decimals (px->dec). */

#define SHEET_COLS      26
#define SHEET_ROWS      999
#define SHEET_CELLS_MAX 1200
#define SHEET_INPUT_MAX 120 /* a cell's text, without the 0 */

typedef enum { CELL_EMPTY, CELL_NUMBER, CELL_TEXT, CELL_FORMULA } cell_kind_t;

typedef enum {
    SHEET_OK,
    SHEET_DIV0,   /* #DIV/0! */
    SHEET_VALUE,  /* #VALUE!: text where a number goes, a range alone */
    SHEET_REF,    /* #REF!: past Z or 999, or a copy that went off the sheet */
    SHEET_NAME,   /* #NAME?: no such function */
    SHEET_NUM,    /* #NUM!: no answer, or too big */
    SHEET_CIRC,   /* #CIRC!: it needs itself, through other cells or not */
    SHEET_SYNTAX, /* #SYNTAX: it can't be read */
} sheet_err_t;

/* A cell as shown: its kind; for a formula its result -- a number, or an
 * error; for text the text (without a leading "'"). */
typedef struct {
    cell_kind_t kind;
    sheet_err_t err;
    px_dec_t num;
    const char *text;
} sheet_value_t;

typedef enum { FMT_GENERAL, FMT_FIX, FMT_SCI } fmt_mode_t;
typedef struct {
    uint8_t mode;   /* fmt_mode_t */
    uint8_t digits; /* FIX: after the point; SCI: after the first digit */
} sheet_fmt_t;

/* A function for the catalog: its name, its arguments, what it does. */
typedef struct {
    const char *name;
    const char *args;
    const char *help;
} sheet_function_t;
extern const sheet_function_t sheet_functions[];
extern const int sheet_function_count;

void sheet_clear(void);
/* Sets cell (row, col) -- from 0 -- to input ("" empties it). false: no
 * room (cells or memory), the cell as it was. Doesn't recalculate. */
bool sheet_set(int row, int col, const char *input);
const char *sheet_input(int row, int col); /* "" if empty */
void sheet_value(int row, int col, sheet_value_t *v);
int sheet_count(void); /* cells in use */
/* The last row and column in use, -1 if none. */
void sheet_extent(int *last_row, int *last_col);

/* Every formula again, in passes: a formula whose cells aren't ready yet
 * waits for the next pass, so a long chain needs no deep recursion. */
void sheet_recalc(void);

/* A formula ("=...") as typed: -1 if it reads, else where it stops. */
int sheet_check(const char *formula);

/* The text for a value in a column width characters wide, *right (numbers
 * and errors) or left (text) -- "####" if a number doesn't fit. */
void sheet_format(char *buf, int width, const sheet_value_t *v, sheet_fmt_t fmt, bool *right);
const char *sheet_error_text(sheet_err_t err);

/* "B12" for row 11, col 1. */
void sheet_ref_name(char *buf, int row, int col);
/* A cell's name at s ("b12", "$B$12"): the characters it takes, 0 if none. */
int sheet_parse_ref(const char *s, int *row, int *col);

/* An input copied drow rows and dcol columns away: a formula's relative
 * references move with it ($ ones don't); one moved off the sheet becomes
 * #REF!. Other inputs are copied as they are. false: no room in out. */
bool sheet_shift(const char *in, int drow, int dcol, char *out, int size);

/* CSV, as Excel writes it: fields separated by sep, quoted ("...", ""
 * for a quote) when they hold sep, a quote or a line end. Reading goes a
 * piece at a time: begin, feed what's read, end. A ';' file may write
 * numbers with a decimal comma. Past Z or row 999 is left out (*lost);
 * false from end() if the cells didn't all fit. */
char sheet_csv_guess_sep(const char *data, int n);
void sheet_csv_begin(char sep);
void sheet_csv_feed(const char *data, int n);
bool sheet_csv_end(bool *lost);
/* The sheet as CSV, comma-separated, CRLF lines, up to its last row and
 * column; out() gets it a piece at a time. false if out() did. */
bool sheet_csv_write(bool (*out)(void *ctx, const char *s, int n), void *ctx);

#endif
