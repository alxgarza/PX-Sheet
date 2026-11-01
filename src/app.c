/* SPREADSHEET for the PX-42F: the grid, the keys, files. The sheet itself
 * -- cells, formulas, CSV -- is sheet.c's.
 *
 * Moving around works as the HP-42S's matrix editor: the arrows go up and
 * down, the menu's soft keys left and right. Typing starts a cell's entry
 * on the top line, with two keyboards (ALPHA = SHIFT ENTER switches): 123,
 * where keys type digits and the operators and the top row the columns'
 * letters A-E; and ABC, where each key types the letter printed on it.
 * SHIFT gives a key's other one. XEQ picks a function, RCL points at a
 * cell. Sheets are CSV files in the app's folder. */
#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "px.h"
#include "sheet.h"

/* ===================== the screen ===================== */

#define W          PX_SCREEN_W
#define CW         6   /* PX_FONT_MINI: the grid, the menu, the lists */
#define BIG_CW     8   /* PX_FONT_SMALL: the top line */
#define TOP_H      13  /* the top line and the rule under it */
#define HEAD_Y     13  /* the columns' letters */
#define GRID_Y     23  /* the first row */
#define ROW_H      9
#define ROWS_SHOWN 10
#define MENU_Y     113 /* the menu or a hint, to the bottom */
#define LABEL_W    20  /* the rows' numbers, 3 digits */
#define LINE_CHARS 41  /* PX_FONT_MINI across the screen */
#define WIDTH_MIN  3
#define WIDTH_MAX  30
#define WIDTH_DEFAULT 9
#define NAME_MAX   20  /* a sheet's file name, without .CSV */

/* The HP-42S characters the hints use. */
#define UP    "\x5e"
#define DOWN  "\x0e"
#define LEFT  "\x10"
#define RIGHT "\x0f"

static int cur_row, cur_col, top_row, left_col;
static uint8_t widths[SHEET_COLS];
static sheet_fmt_t fmt = {FMT_GENERAL, 2};
static char sheet_name[NAME_MAX + 1] = "SHEET1";
static bool modified;
static bool shift;
static char message[48]; /* in the bottom line, until the next key */

/* A block of cells marked (COPY, then moving): from here to the cursor. */
static bool marking;
static int mark_row, mark_col;

/* The entry being typed, shown on the top line instead of the cell's. */
static const char *entry;
static int entry_cursor;

static jmp_buf quit_jump; /* PX_KEY_QUIT: save what's needed and return */

static void say(const char *s) {
    snprintf(message, sizeof(message), "%s", s);
}

/* What's typed in cells, as the fonts draw it: ^ is the HP-42S's code 30
 * (94 is its up arrow), and what they have no character for is '?'. */
static void draw_text(int x, int y, const char *s, int n, int font, bool inverse) {
    char buf[64];
    int k = 0;
    for (int i = 0; i < n && s[i] && k < (int) sizeof(buf) - 1; i++) {
        unsigned char c = (unsigned char) s[i];
        buf[k++] = c == '^' ? 30 : c < 32 || c > 126 ? '?' : (char) c;
    }
    buf[k] = 0;
    px->text(x, y, buf, font, inverse);
}

static void selection(int *r0, int *c0, int *r1, int *c1) {
    *r0 = marking && mark_row < cur_row ? mark_row : cur_row;
    *r1 = marking && mark_row > cur_row ? mark_row : cur_row;
    *c0 = marking && mark_col < cur_col ? mark_col : cur_col;
    *c1 = marking && mark_col > cur_col ? mark_col : cur_col;
}

/* Keeps the cursor on the screen. */
static void scroll(void) {
    if (cur_row < top_row)
        top_row = cur_row;
    if (cur_row >= top_row + ROWS_SHOWN)
        top_row = cur_row - ROWS_SHOWN + 1;
    if (cur_col < left_col)
        left_col = cur_col;
    for (;;) {
        int x = LABEL_W;
        for (int c = left_col; c < cur_col; c++)
            x += widths[c] * CW + 1;
        if (x + widths[cur_col] * CW <= W || left_col == cur_col)
            break;
        left_col++;
    }
}

static void draw_grid(void) {
    int r0, c0, r1, c1;
    selection(&r0, &c0, &r1, &c1);
    int x = LABEL_W;
    for (int col = left_col; col < SHEET_COLS; col++) {
        int w = widths[col] * CW;
        if (x + w > W)
            break;
        bool on = col >= c0 && col <= c1;
        char name[2] = {(char) ('A' + col), 0};
        if (on)
            px->fill(x, HEAD_Y, w, ROW_H - 1, 1);
        px->text(x + (w - CW) / 2, HEAD_Y, name, PX_FONT_MINI, on);
        for (int i = 0; i < ROWS_SHOWN && top_row + i < SHEET_ROWS; i++) {
            int row = top_row + i, y = GRID_Y + i * ROW_H;
            sheet_value_t v;
            char text[WIDTH_MAX + 1];
            bool right;
            sheet_value(row, col, &v);
            sheet_format(text, widths[col], &v, fmt, &right);
            bool sel = on && row >= r0 && row <= r1;
            if (sel)
                px->fill(x - 1, y - 1, w + 1, ROW_H, 1);
            int n = (int) strlen(text);
            draw_text(right ? x + w - n * CW : x, y, text, n, PX_FONT_MINI, sel);
        }
        for (int y = HEAD_Y; y < MENU_Y - 1; y += 2) /* dotted, between columns */
            px->pixel(x + w, y, 1);
        x += w + 1;
    }
    px->line(0, GRID_Y - 2, W - 1, GRID_Y - 2, 1);
    for (int i = 0; i < ROWS_SHOWN && top_row + i < SHEET_ROWS; i++) {
        int row = top_row + i, y = GRID_Y + i * ROW_H;
        char label[8];
        int n = snprintf(label, sizeof(label), "%d", row + 1);
        bool on = row >= r0 && row <= r1;
        if (on)
            px->fill(0, y - 1, LABEL_W - 1, ROW_H, 1);
        px->text(LABEL_W - 2 - n * CW, y, label, PX_FONT_MINI, on);
    }
}

/* The top line: the entry being typed, with its cursor; or the cell's
 * name and what's typed in it. */
static void draw_top(void) {
    int chars = W / BIG_CW;
    if (entry != NULL) {
        int len = (int) strlen(entry);
        int first = entry_cursor - (chars - 1);
        if (first < 0)
            first = 0;
        draw_text(0, 0, entry + first, chars, PX_FONT_SMALL, false);
        char under[2] = {entry_cursor < len ? entry[entry_cursor] : ' ', 0};
        draw_text((entry_cursor - first) * BIG_CW, 0, under, 1, PX_FONT_SMALL, true);
    } else {
        char name[16];
        int r0, c0, r1, c1;
        selection(&r0, &c0, &r1, &c1);
        sheet_ref_name(name, r0, c0);
        if (marking) {
            size_t n = strlen(name);
            name[n++] = ':';
            sheet_ref_name(name + n, r1, c1);
        }
        int x = px->text(0, 0, name, PX_FONT_SMALL, true);
        if (!marking)
            draw_text(x + BIG_CW, 0, sheet_input(cur_row, cur_col), chars - x / BIG_CW - 1, PX_FONT_SMALL, false);
    }
    px->line(0, TOP_H - 1, W - 1, TOP_H - 1, 1);
}

/* The soft keys' labels along the bottom, or the message. */
static void draw_menu(const char *const labels[6]) {
    if (message[0]) {
        px->text(1, MENU_Y + 1, message, PX_FONT_MINI, false);
        return;
    }
    for (int i = 0; i < 6; i++) {
        int x0 = i * W / 6, x1 = (i + 1) * W / 6 - 2;
        if (labels[i][0] == 0)
            continue;
        px->fill(x0, MENU_Y, x1 - x0 + 1, PX_SCREEN_H - MENU_Y, 1);
        int n = (int) strlen(labels[i]);
        px->text(x0 + (x1 - x0 + 1 - n * CW) / 2, MENU_Y + 1, labels[i], PX_FONT_MINI, true);
    }
}

static void draw_hint(const char *s) {
    px->text(1, MENU_Y + 1, message[0] ? message : s, PX_FONT_MINI, false);
}

/* ===================== keys ===================== */

/* The next key going down; held, the arrows (and the soft keys that
 * move) repeat. SHIFT is a key here, as on the calculator. */
static int held = -1;
static uint32_t repeat_at;
static bool repeat_soft;

static bool repeats(int k) {
    return k == PX_KEY_ARROW_UP || k == PX_KEY_ARROW_DN || k == PX_KEY_BACKSPACE
           || (repeat_soft && (k == PX_KEY(0, 0) || k == PX_KEY(0, 1)));
}

static int next_key(void) {
    for (;;) {
        int wait = -1;
        if (held >= 0) {
            int32_t left = (int32_t) (repeat_at - px->millis());
            wait = left > 0 ? (int) left : 0;
        }
        int k = px->key(wait);
        if (k == PX_KEY_QUIT)
            longjmp(quit_jump, 1);
        if (k == PX_KEY_NONE) {
            if (held < 0)
                continue;
            repeat_at = px->millis() + 70;
            return held;
        }
        if (k & PX_KEY_RELEASED) {
            if ((k & ~PX_KEY_RELEASED) == held)
                held = -1;
            continue;
        }
        held = repeats(k) ? k : -1;
        repeat_at = px->millis() + 400;
        message[0] = 0;
        return k;
    }
}

/* What each key types: on ABC its printed letter, on 123 its digit or
 * symbol, or the letter where it has none. SHIFT gives the other. */
static const char letters[7][6] = {
    {'A', 'B', 'C', 'D', 'E', 'F'},
    {'G', 'H', 'I', 'J', 'K', 'L'},
    {0, 'M', 'N', 'O', 0, 0},
    {0, 'P', 'Q', 'R', 'S', 0},
    {0, 'T', 'U', 'V', 'W', 0},
    {0, 'X', 'Y', 'Z', '_', 0},
    {0, ':', ',', '?', ' ', 0},
};
static const char symbols[7][6] = {
    {0, 0, 0, 0, 0, 0},           /* A-E: the columns, on 123 too; XEQ: a function */
    {'=', 0, '%', '(', ')', '$'}, /* STO, RCL (a cell), R-down, SIN, COS, TAN */
    {0, '<', '>', 'E', 0, 0},     /* x<>y, +/-, E */
    {0, '7', '8', '9', '/', 0},
    {0, '4', '5', '6', '*', 0},
    {0, '1', '2', '3', '-', 0},
    {0, '0', '.', '^', '+', 0},
};

#define KEY_RCL PX_KEY(1, 1)

static char key_char(int k, bool abc) {
    int r = PX_KEY_ROW(k), c = PX_KEY_COL(k);
    if (r > 6 || c > 5)
        return 0;
    char letter = letters[r][c], symbol = symbols[r][c] ? symbols[r][c] : letter;
    return abc != shift ? letter : symbol;
}

/* XEQ and RCL: on 123 as they are, on ABC with SHIFT. */
static bool key_is(int k, int which, bool abc) {
    return k == which && abc == shift;
}

/* ===================== lists and questions ===================== */

/* A list to pick from, full screen: its index, or -1 for EXIT. */
static int pick(const char *title, const char *const *rows, int n, int at, const char *hint) {
    const int shown = 10;
    int first = 0;
    for (;;) {
        if (at < first)
            first = at;
        if (at >= first + shown)
            first = at - shown + 1;
        px->clear();
        px->fill(0, 0, W, 12, 1);
        px->text(1, 0, title, PX_FONT_SMALL, true);
        for (int i = 0; i < shown && first + i < n; i++) {
            int y = 15 + i * ROW_H;
            bool on = first + i == at;
            if (on)
                px->fill(0, y - 1, W, ROW_H, 1);
            draw_text(2, y, rows[first + i], LINE_CHARS, PX_FONT_MINI, on);
        }
        draw_hint(hint);
        px->show();
        repeat_soft = false;
        int k = next_key();
        if (k == PX_KEY_SHIFT) {
            shift = !shift;
            continue;
        }
        int step = shift ? shown : 1;
        shift = false;
        if (k == PX_KEY_ON)
            return -1;
        if (k == PX_KEY_ENTER && n > 0)
            return at;
        if (k == PX_KEY_ARROW_UP)
            at = at - step < 0 ? (step == 1 ? n - 1 : 0) : at - step;
        else if (k == PX_KEY_ARROW_DN)
            at = at + step >= n ? (step == 1 ? 0 : n - 1) : at + step;
        else if (key_char(k, true) >= 'A' && key_char(k, true) <= 'Z') /* to the first with that letter */
            for (int i = 0; i < n; i++)
                if (toupper((unsigned char) rows[i][0]) == key_char(k, true)) {
                    at = i;
                    break;
                }
    }
}

/* A question on the screen: ENTER 1, <- 2, EXIT 0. */
static int ask(const char *line1, const char *keys) {
    px->clear();
    px->text(4, 40, line1, PX_FONT_SMALL, false);
    px->text(1, MENU_Y + 1, keys, PX_FONT_MINI, false);
    px->show();
    for (;;) {
        int k = next_key();
        if (k == PX_KEY_ENTER)
            return 1;
        if (k == PX_KEY_BACKSPACE)
            return 2;
        if (k == PX_KEY_ON)
            return 0;
    }
}

/* ===================== the entry ===================== */

static void draw(const char *const labels[6], const char *hint);

static void insert(char *buf, int size, int *cursor, const char *s) {
    int len = (int) strlen(buf), n = (int) strlen(s);
    if (len + n > size - 1) {
        say("The entry is full");
        return;
    }
    memmove(buf + *cursor + n, buf + *cursor, (size_t) (len - *cursor + 1));
    memcpy(buf + *cursor, s, (size_t) n);
    *cursor += n;
}

/* XEQ: a function from the catalog, as "NAME(" -- "PI()" whole. */
static bool catalog(char *out, int size) {
    static char rows[64][LINE_CHARS + 1];
    static const char *list[64];
    static int last;
    for (int i = 0; i < sheet_function_count; i++) {
        const sheet_function_t *f = &sheet_functions[i];
        char call[24];
        snprintf(call, sizeof(call), "%s(%s)", f->name, f->args);
        snprintf(rows[i], sizeof(rows[i]), "%-17s %s", call, f->help);
        list[i] = rows[i];
    }
    int i = pick("Functions", list, sheet_function_count, last, "ENTER:insert  EXIT:back  A-Z:jump");
    if (i < 0)
        return false;
    last = i;
    snprintf(out, (size_t) size, "%s(%s", sheet_functions[i].name, sheet_functions[i].args[0] ? "" : ")");
    return true;
}

/* RCL: the name of a cell or a block, picked on the grid with the arrows
 * and 8 4 6 2; '.' starts a block. */
static bool point(char *out) {
    int save_row = cur_row, save_col = cur_col;
    bool done = false;
    marking = false;
    for (;;) {
        scroll();
        draw(NULL, marking ? "8 4 6 2 " UP DOWN ":move  ENTER:insert block"
                           : "8 4 6 2 " UP DOWN ":move  .:block  ENTER:insert");
        repeat_soft = false;
        int k = next_key();
        if (k == PX_KEY_SHIFT) {
            shift = !shift;
            continue;
        }
        int step = shift ? ROWS_SHOWN : 1;
        shift = false;
        if (k == PX_KEY_ARROW_UP || k == PX_KEY(3, 2)) {
            cur_row = cur_row - step < 0 ? 0 : cur_row - step;
        } else if (k == PX_KEY_ARROW_DN || k == PX_KEY(5, 2)) {
            cur_row = cur_row + step >= SHEET_ROWS ? SHEET_ROWS - 1 : cur_row + step;
        } else if (k == PX_KEY(4, 1)) {
            if (cur_col > 0)
                cur_col--;
        } else if (k == PX_KEY(4, 3)) {
            if (cur_col < SHEET_COLS - 1)
                cur_col++;
        } else if (k == PX_KEY(6, 2) || k == PX_KEY(6, 1)) {
            marking = !marking;
            mark_row = cur_row;
            mark_col = cur_col;
        } else if (k == PX_KEY_ENTER) {
            int r0, c0, r1, c1;
            selection(&r0, &c0, &r1, &c1);
            sheet_ref_name(out, r0, c0);
            if (marking) {
                size_t n = strlen(out);
                out[n++] = ':';
                sheet_ref_name(out + n, r1, c1);
            }
            done = true;
            break;
        } else if (k == PX_KEY_ON) {
            break;
        }
    }
    marking = false;
    cur_row = save_row;
    cur_col = save_col;
    scroll();
    return done;
}

#define ENTRY_CELL 1 /* XEQ and RCL work: a cell's entry */

/* Edits buf on the top line, the cursor at `cursor` (-1: the end): true
 * for ENTER, false for EXIT. */
static bool edit(char *buf, int size, bool abc, int flags, const char *what, int cursor) {
    if (cursor < 0 || cursor > (int) strlen(buf))
        cursor = (int) strlen(buf);
    entry = buf;
    bool ok = false;
    for (;;) {
        char hint[48];
        snprintf(hint, sizeof(hint), "%s %s%s ALPHA:%s%s", what, shift ? "SHIFT " : "", abc ? "ABC" : "123",
                 abc ? "123" : "ABC",
                 !(flags & ENTRY_CELL) ? "" : abc ? " SHIFT XEQ:fn RCL:cell" : "  XEQ:fn  RCL:cell");
        entry_cursor = cursor;
        draw(NULL, hint);
        repeat_soft = false;
        int k = next_key();
        if (k == PX_KEY_SHIFT) {
            shift = !shift;
            continue;
        }
        bool was_shift = shift;
        if (k == PX_KEY_ON) {
            shift = false;
            break;
        } else if (k == PX_KEY_ENTER && was_shift) {
            abc = !abc;
        } else if (k == PX_KEY_ENTER) {
            ok = true;
            shift = false;
            break;
        } else if (k == PX_KEY_BACKSPACE) {
            if (was_shift) {
                buf[0] = 0;
                cursor = 0;
            } else if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor, strlen(buf + cursor) + 1);
                cursor--;
            }
        } else if (k == PX_KEY_ARROW_UP) {
            cursor = was_shift || cursor == 0 ? 0 : cursor - 1;
        } else if (k == PX_KEY_ARROW_DN) {
            int len = (int) strlen(buf);
            cursor = was_shift || cursor >= len ? len : cursor + 1;
        } else if ((flags & ENTRY_CELL) && key_is(k, PX_KEY_XEQ, abc)) {
            char call[24];
            shift = false;
            if (catalog(call, sizeof(call)))
                insert(buf, size, &cursor, call);
        } else if ((flags & ENTRY_CELL) && key_is(k, KEY_RCL, abc)) {
            char name[16];
            shift = false;
            entry_cursor = cursor;
            if (point(name))
                insert(buf, size, &cursor, name);
        } else {
            char c = key_char(k, abc);
            if (c) {
                char s[2] = {c, 0};
                insert(buf, size, &cursor, s);
            }
        }
        shift = false;
    }
    entry = NULL;
    return ok;
}

/* ===================== the sheet's screen ===================== */

static const char *const menu_main[6] = {LEFT, RIGHT, "GOTO", "COPY", "PASTE", "FILE"};
static const char *const menu_shift[6] = {"WIDTH", "FORMAT", "CLEAR", "CALC", "", "HELP"};
static const char *const menu_mark[6] = {LEFT, RIGHT, "CLEAR", "COPY", "PASTE", ""};

static void draw(const char *const labels[6], const char *hint) {
    px->clear();
    draw_grid();
    draw_top();
    if (labels != NULL)
        draw_menu(labels);
    else
        draw_hint(hint);
    px->show();
}

static void changed(void) {
    sheet_recalc();
    modified = true;
}

/* Typing into the cursor's cell, starting with `text`; the cursor goes
 * down after ENTER. A formula that can't be read is kept on the top line,
 * its cursor where reading stopped. */
static void enter_cell(const char *text, bool abc, bool pointing) {
    char buf[SHEET_INPUT_MAX + 1], what[8];
    snprintf(buf, sizeof(buf), "%s", text);
    sheet_ref_name(what, cur_row, cur_col);
    if (pointing) {
        char name[16];
        entry = buf;
        entry_cursor = (int) strlen(buf);
        if (point(name))
            strncat(buf, name, sizeof(buf) - strlen(buf) - 1);
    }
    int cursor = -1;
    while (edit(buf, sizeof(buf), abc, ENTRY_CELL, what, cursor)) {
        if (buf[0] == '=' && (cursor = sheet_check(buf)) >= 0) {
            say("Can't read it: fix it at the cursor");
        } else if (!sheet_set(cur_row, cur_col, buf)) {
            say("Memory full");
        } else {
            changed();
            if (cur_row < SHEET_ROWS - 1)
                cur_row++;
            return;
        }
        px->tone_async(10);
    }
}

/* ----- copy and paste ----- */

typedef struct {
    uint16_t row, col; /* from the block's top left */
    char *text;
} clip_cell_t;

static clip_cell_t *clip;
static int clip_count, clip_rows, clip_cols, clip_row0, clip_col0;

static void clip_free(void) {
    for (int i = 0; i < clip_count; i++)
        free(clip[i].text);
    free(clip);
    clip = NULL;
    clip_count = clip_rows = clip_cols = 0;
}

static void copy_block(int r0, int c0, int r1, int c1) {
    clip_free();
    int n = 0;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            n += sheet_input(r, c)[0] != 0;
    clip = n ? malloc((size_t) n * sizeof(clip_cell_t)) : NULL;
    for (int r = r0; r <= r1 && (clip || !n); r++)
        for (int c = c0; c <= c1; c++) {
            const char *t = sheet_input(r, c);
            char *copy = t[0] ? malloc(strlen(t) + 1) : NULL;
            if (copy == NULL)
                continue;
            strcpy(copy, t);
            clip[clip_count++] = (clip_cell_t){(uint16_t) (r - r0), (uint16_t) (c - c0), copy};
        }
    if (clip_count < n) {
        clip_free();
        say("Memory full: nothing copied");
        return;
    }
    clip_rows = r1 - r0 + 1;
    clip_cols = c1 - c0 + 1;
    clip_row0 = r0;
    clip_col0 = c0;
    char s[48];
    snprintf(s, sizeof(s), "Copied %d x %d: PASTE puts it", clip_rows, clip_cols);
    say(s);
}

static const char *clip_at(int r, int c) {
    for (int i = 0; i < clip_count; i++)
        if (clip[i].row == r && clip[i].col == c)
            return clip[i].text;
    return "";
}

/* The copy's cell (r, c) into (row, col), its names moved with it. */
static bool paste_cell(int row, int col, int r, int c) {
    char text[SHEET_INPUT_MAX + 1];
    if (!sheet_shift(clip_at(r, c), row - (clip_row0 + r), col - (clip_col0 + c), text, sizeof(text))
            || !sheet_set(row, col, text)) {
        say("Memory full");
        return false;
    }
    return true;
}

/* The copy at the cursor; into a marked block, over and over to fill it
 * (a row copied, then a column marked: Excel's fill down). */
static void paste(void) {
    if (clip_rows == 0) {
        say("Nothing copied yet: COPY first");
        return;
    }
    int r0, c0, r1, c1;
    selection(&r0, &c0, &r1, &c1);
    if (!marking) {
        r1 = r0 + clip_rows - 1;
        c1 = c0 + clip_cols - 1;
    } else if (c1 - c0 + 1 < clip_cols) { /* a row copied, one column marked: the row's width */
        c1 = c0 + clip_cols - 1;
    }
    bool ok = true;
    for (int row = r0; ok && row <= r1 && row < SHEET_ROWS; row++)
        for (int col = c0; ok && col <= c1 && col < SHEET_COLS; col++)
            ok = paste_cell(row, col, (row - r0) % clip_rows, (col - c0) % clip_cols);
    marking = false;
    changed();
}

static void clear_block(void) {
    int r0, c0, r1, c1;
    selection(&r0, &c0, &r1, &c1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            sheet_set(r, c, "");
    marking = false;
    changed();
}

/* ----- files ----- */

#define STATE_FILE   "STATE"
#define RECOVER_FILE "RECOVER.TMP"

/* What the app remembers between runs, in its folder. */
typedef struct {
    uint8_t version;
    uint8_t recover; /* RECOVER_FILE holds changes not saved */
    uint8_t fmt_mode, fmt_digits;
    char name[NAME_MAX + 1];
    uint8_t widths[SHEET_COLS];
} state_t;
#define STATE_VERSION 1

static void save_state(bool recover) {
    state_t s = {STATE_VERSION, recover, fmt.mode, fmt.digits, {0}, {0}};
    memcpy(s.name, sheet_name, sizeof(s.name));
    memcpy(s.widths, widths, sizeof(widths));
    px->save(STATE_FILE, &s, sizeof(s));
}

static char out_buf[512];
static int out_len, out_file;

static bool out_flush(void) {
    bool ok = out_len == 0 || px->write(out_file, out_buf, out_len) == out_len;
    out_len = 0;
    return ok;
}

static bool out(void *ctx, const char *s, int n) {
    (void) ctx;
    while (n > 0) {
        if (out_len == (int) sizeof(out_buf) && !out_flush())
            return false;
        int k = (int) sizeof(out_buf) - out_len;
        if (k > n)
            k = n;
        memcpy(out_buf + out_len, s, (size_t) k);
        out_len += k;
        s += k;
        n -= k;
    }
    return true;
}

static bool write_csv(const char *path) {
    out_file = px->open(path, "w");
    if (out_file < 0)
        return false;
    out_len = 0;
    bool ok = sheet_csv_write(out, NULL) && out_flush();
    return px->close(out_file) == 0 && ok;
}

static bool save_as(const char *name) {
    char path[NAME_MAX + 8];
    snprintf(path, sizeof(path), "%s.CSV", name);
    if (!write_csv(path)) {
        say("Can't save: is the drive full?");
        px->tone_async(10);
        return false;
    }
    snprintf(sheet_name, sizeof(sheet_name), "%s", name);
    modified = false;
    save_state(false);
    char s[48];
    snprintf(s, sizeof(s), "Saved %s.CSV", name);
    say(s);
    return true;
}

/* A CSV into the sheet, in place of what's there. */
static bool read_csv(const char *path) {
    int f = px->open(path, "r");
    if (f < 0)
        return false;
    sheet_clear();
    static char buf[512];
    bool first = true, lost;
    int n;
    while ((n = px->read(f, buf, sizeof(buf))) > 0) {
        if (first)
            sheet_csv_begin(sheet_csv_guess_sep(buf, n));
        first = false;
        sheet_csv_feed(buf, n);
    }
    px->close(f);
    if (first)
        sheet_csv_begin(',');
    bool fit = sheet_csv_end(&lost);
    sheet_recalc();
    if (!fit)
        say("Memory full: only part of it read");
    else if (lost)
        say("Past Z999, or too long: left out");
    return true;
}

static void reset_view(void) {
    cur_row = cur_col = top_row = left_col = 0;
    marking = false;
}

/* Changes not saved: ENTER saves them, <- drops them; false for EXIT. */
static bool settle(void) {
    if (!modified)
        return true;
    char line[40];
    snprintf(line, sizeof(line), "Save changes to %s?", sheet_name);
    int a = ask(line, "ENTER:save  " LEFT ":don't save  EXIT:back");
    return a == 2 || (a == 1 && save_as(sheet_name));
}

/* A file name typed on the top line: letters, digits, - and _. */
static bool ask_name(char *name, const char *what) {
    char buf[NAME_MAX + 1];
    snprintf(buf, sizeof(buf), "%s", name);
    for (;;) {
        if (!edit(buf, sizeof(buf), true, 0, what, -1))
            return false;
        bool good = buf[0] != 0;
        for (char *c = buf; *c; c++) {
            *c = (char) toupper((unsigned char) *c);
            good = good && (isalnum((unsigned char) *c) || *c == '-' || *c == '_');
        }
        if (good) {
            strcpy(name, buf);
            return true;
        }
        say("Letters, digits, - and _ only");
    }
}

static void open_menu(void) {
    static char names[40][PX_FILE_NAME_MAX + 1];
    static const char *list[40];
    int n = px->list("", ".CSV", names, 40);
    if (n == 0) {
        say("No .CSV in BIN/APPS/SPREADSHEET");
        return;
    }
    for (int k = 0; k < n; k++)
        list[k] = names[k];
    int k = pick("Open", list, n, 0, "ENTER:open  EXIT:back");
    if (k < 0 || !settle())
        return;
    char path[PX_FILE_NAME_MAX + 8];
    snprintf(path, sizeof(path), "%s.CSV", names[k]);
    if (read_csv(path)) {
        snprintf(sheet_name, sizeof(sheet_name), "%.*s", NAME_MAX, names[k]);
        modified = false;
        reset_view();
        save_state(false);
    }
}

static void file_menu(void) {
    char save_row[40];
    snprintf(save_row, sizeof(save_row), "Save %s.CSV", sheet_name);
    const char *rows[] = {save_row, "Save as...", "Open...", "New sheet"};
    int i = pick("File", rows, 4, 0, "ENTER:choose  EXIT:back");
    char name[NAME_MAX + 1];
    if (i == 0) {
        save_as(sheet_name);
    } else if (i == 1) {
        snprintf(name, sizeof(name), "%s", sheet_name);
        if (ask_name(name, "Save as"))
            save_as(name);
    } else if (i == 2) {
        open_menu();
    } else if (i == 3) {
        name[0] = 0;
        if (!settle() || !ask_name(name, "New sheet"))
            return;
        sheet_clear();
        snprintf(sheet_name, sizeof(sheet_name), "%s", name);
        modified = false;
        reset_view();
        save_state(false);
    }
}

static void format_menu(void) {
    static const char *const rows[] = {"General", "Fix 0", "Fix 1", "Fix 2", "Fix 3", "Fix 4", "Fix 6",
                                       "Sci 1", "Sci 2", "Sci 3", "Sci 4", "Sci 6"};
    static const sheet_fmt_t fmts[] = {{FMT_GENERAL, 0}, {FMT_FIX, 0}, {FMT_FIX, 1}, {FMT_FIX, 2},
                                       {FMT_FIX, 3}, {FMT_FIX, 4}, {FMT_FIX, 6}, {FMT_SCI, 1},
                                       {FMT_SCI, 2}, {FMT_SCI, 3}, {FMT_SCI, 4}, {FMT_SCI, 6}};
    int at = 0;
    for (int i = 0; i < 12; i++)
        if (fmts[i].mode == fmt.mode && (fmt.mode == FMT_GENERAL || fmts[i].digits == fmt.digits))
            at = i;
    int i = pick("Number format", rows, 12, at, "ENTER:choose  EXIT:back");
    if (i >= 0) {
        fmt = fmts[i];
        save_state(false);
    }
}

static void width_mode(void) {
    for (;;) {
        char hint[48];
        snprintf(hint, sizeof(hint), "Column %c: %d wide  " UP DOWN ":change  ENTER:done", 'A' + cur_col,
                 widths[cur_col]);
        scroll();
        draw(NULL, hint);
        repeat_soft = false;
        int k = next_key();
        if (k == PX_KEY_ARROW_UP && widths[cur_col] < WIDTH_MAX)
            widths[cur_col]++;
        else if (k == PX_KEY_ARROW_DN && widths[cur_col] > WIDTH_MIN)
            widths[cur_col]--;
        else if (k == PX_KEY_ENTER || k == PX_KEY_ON)
            break;
    }
    save_state(false);
}

static void go_to(void) {
    char buf[8] = "";
    if (!edit(buf, sizeof(buf), true, 0, "Go to", -1))
        return;
    int row, col, n = sheet_parse_ref(buf, &row, &col);
    if (n == 0 || buf[n] != 0) {
        say("Not a cell: A1 to Z999");
        return;
    }
    cur_row = row;
    cur_col = col;
}

static void help(void) {
    static const char *const lines[] = {
        UP " " DOWN " rows,  " LEFT " " RIGHT " columns (the menu)",
        "SHIFT " UP DOWN ": ten rows at a time",
        "Digits and + - * / start a number or a",
        "formula. STO types =, RCL points at a",
        "cell. ALPHA (SHIFT ENTER): text.",
        "ENTER edits the cell, " LEFT " empties it.",
        "Typing: ALPHA switches 123 and ABC;",
        "SHIFT a key gives its other character;",
        UP DOWN " move; XEQ: functions; RCL: a cell.",
        "COPY, move, COPY: a block. PASTE puts",
        "it, or fills a marked block with one.",
        "Sheets: .CSV in BIN/APPS/SPREADSHEET",
    };
    px->clear();
    for (int i = 0; i < 12; i++)
        px->text(1, i * ROW_H + 1, lines[i], PX_FONT_MINI, false);
    px->text(1, MENU_Y + 1, "Any key: back", PX_FONT_MINI, false);
    px->show();
    next_key();
}

static void move(int drow, int dcol) {
    cur_row += drow;
    cur_col += dcol;
    cur_row = cur_row < 0 ? 0 : cur_row >= SHEET_ROWS ? SHEET_ROWS - 1 : cur_row;
    cur_col = cur_col < 0 ? 0 : cur_col >= SHEET_COLS ? SHEET_COLS - 1 : cur_col;
}

/* ===================== the app ===================== */

static void start(void) {
    for (int i = 0; i < SHEET_COLS; i++)
        widths[i] = WIDTH_DEFAULT;
    state_t s;
    if (px->load(STATE_FILE, &s, sizeof(s)) != (int) sizeof(s) || s.version != STATE_VERSION)
        return;
    s.name[NAME_MAX] = 0;
    snprintf(sheet_name, sizeof(sheet_name), "%s", s.name);
    if (s.fmt_mode <= FMT_SCI && s.fmt_digits <= 9)
        fmt = (sheet_fmt_t){s.fmt_mode, s.fmt_digits};
    for (int i = 0; i < SHEET_COLS; i++)
        if (s.widths[i] >= WIDTH_MIN && s.widths[i] <= WIDTH_MAX)
            widths[i] = s.widths[i];
    char path[NAME_MAX + 8];
    snprintf(path, sizeof(path), "%s.CSV", sheet_name);
    if (s.recover && read_csv(RECOVER_FILE)) {
        modified = true;
        say("Back as you left it: not saved yet");
    } else {
        read_csv(path);
    }
}

/* A key on the sheet's screen. */
static void sheet_key(int k, bool was_shift) {
    int soft = PX_KEY_ROW(k) == 0 ? PX_KEY_COL(k) : -1;
    if (k == PX_KEY_ARROW_UP) {
        move(was_shift ? -ROWS_SHOWN : -1, 0);
    } else if (k == PX_KEY_ARROW_DN) {
        move(was_shift ? ROWS_SHOWN : 1, 0);
    } else if ((soft == 0 || soft == 1) && !was_shift) {
        move(0, soft == 0 ? -1 : 1);
    } else if (marking) {
        int r0, c0, r1, c1;
        selection(&r0, &c0, &r1, &c1);
        if (k == PX_KEY_ON) {
            marking = false;
        } else if (soft == 2 || k == PX_KEY_BACKSPACE) {
            clear_block();
        } else if (soft == 3) {
            copy_block(r0, c0, r1, c1);
            marking = false;
        } else if (soft == 4) {
            paste();
        }
    } else if (soft >= 0 && was_shift) {
        if (soft == 0) {
            width_mode();
        } else if (soft == 1) {
            format_menu();
        } else if (soft == 2) {
            if (ask("Empty the whole sheet?", "ENTER:empty it  EXIT:back") == 1) {
                sheet_clear();
                changed();
                reset_view();
            }
        } else if (soft == 3) {
            sheet_recalc();
            say("Worked out again");
        } else if (soft == 5) {
            help();
        }
    } else if (soft == 2) {
        go_to();
    } else if (soft == 3) {
        marking = true;
        mark_row = cur_row;
        mark_col = cur_col;
        say("Move to mark a block, then COPY");
    } else if (soft == 4) {
        paste();
    } else if (soft == 5) {
        file_menu();
    } else if (k == PX_KEY_ENTER) {
        sheet_value_t v;
        sheet_value(cur_row, cur_col, &v);
        if (was_shift) /* ALPHA: text, typed on ABC */
            enter_cell("", true, false);
        else
            enter_cell(sheet_input(cur_row, cur_col), v.kind == CELL_TEXT, false);
    } else if (k == PX_KEY_BACKSPACE) {
        if (sheet_input(cur_row, cur_col)[0]) {
            sheet_set(cur_row, cur_col, "");
            changed();
        }
    } else if (k == KEY_RCL && !was_shift) {
        enter_cell("=", false, true);
    } else {
        shift = was_shift; /* the key's 123 character, or its letter with SHIFT */
        char first[2] = {key_char(k, false), 0};
        shift = false;
        if (first[0])
            enter_cell(first, false, false);
    }
}

int app_main(void) {
    start();
    px->on_key(1); /* EXIT asks to save first */
    if (setjmp(quit_jump)) {
        /* The Sleep Timer, ON held, an empty battery: no time to ask. */
        save_state(modified && write_csv(RECOVER_FILE));
        return PX_EXIT_MENU;
    }
    for (;;) {
        scroll();
        draw(marking ? menu_mark : shift ? menu_shift : menu_main, NULL);
        repeat_soft = true;
        int k = next_key();
        if (k == PX_KEY_SHIFT) {
            shift = !shift;
            continue;
        }
        bool was_shift = shift;
        shift = false;
        if (k == PX_KEY_ON && !marking) {
            if (settle()) {
                save_state(false);
                return PX_EXIT_MENU;
            }
            continue;
        }
        sheet_key(k, was_shift);
    }
}
