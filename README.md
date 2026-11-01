# PX-42F Spreadsheet

A spreadsheet for the **PX-42F** calculator: a grid of cells A1 to Z999 with
formulas, worked out in the calculator's own 34-digit decimals, so 0.1 + 0.2
is exactly 0.3. Sheets are CSV files: make one on the computer, work on it on
the calculator, open it again in Excel or Numbers with its formulas.

It is a native app, `SPREADSHEET.bin`, built from the
[PX-42F App Template](../PX-42F-App-Template). It runs from
**Settings → Games & Apps**.

## The screen

- **The top line** shows the cell's name and what's typed in it. While you type,
  it shows your entry with its cursor.
- **The grid** shows 10 rows and as many columns as fit. Numbers are right-aligned,
  text left. A number too wide for its column shows `####`.
- **The bottom line** shows the soft keys' menu, a hint while you type, or a
  message.

## Keys

| On the sheet | |
|---|---|
| ↑ ↓ | Up and down a row. SHIFT first: ten rows. |
| ← → (the first two menu keys) | Left and right a column, as in the HP-42S matrix editor. |
| A digit, `.`, + − × ÷ | Starts a number or a formula in the cell. |
| STO | Starts a formula: types `=`. |
| RCL | Starts a formula and points at a cell (below). |
| ALPHA (SHIFT ENTER) | Starts text, on the ABC keyboard. |
| ENTER | Edits what's in the cell. |
| ← (backspace) | Empties the cell. |
| EXIT | Leaves the app. If the sheet has changes, it asks to save them first. |

| Menu | SHIFT menu |
|---|---|
| ← → GOTO COPY PASTE FILE | WIDTH FORMAT CLEAR CALC · HELP |

- **GOTO** goes to a cell by name, such as `C12`.
- **COPY** marks a block: move to its other corner, then press COPY again.
  **PASTE** puts the block at the cursor. Relative names in its formulas move
  with it; `$A$1` names don't. Copy one cell, mark a block, then PASTE to fill
  the block with it, as Excel's fill down does.
- **FILE** has Save, Save as, Open and New sheet.
- **WIDTH** changes the cursor's column width (3 to 30) with ↑ ↓.
- **FORMAT** is General, Fix 0-6 or Sci 1-6, for the whole sheet.
- **CLEAR** empties the whole sheet.
- **CALC** works everything out again. RAND() changes then.

### Typing

There are two keyboards. ALPHA (SHIFT ENTER) switches between them. SHIFT before
a key gives its character on the other keyboard.

- **123**: digits, `. + - * / ^`, and the top row's A–E for column names, so
  `=A1+B1` needs no SHIFT. STO is `=`, SIN `(`, COS `)`, TAN `$`, x<>y `<`,
  +/- `>`, R↓ `%`, R/S `^`, E is the exponent's `E`. SHIFT and a key types its
  letter.
- **ABC**: each key types the letter printed on it. SHIFT and a key types its
  123 character.
- ↑ ↓ move the cursor in the entry; SHIFT ↑ ↓ go to its start or end. ←
  deletes; SHIFT ← clears it. ENTER stores it, EXIT drops it.
- **XEQ** (SHIFT XEQ on ABC) opens the function catalog. ENTER types the
  function's name and `(`.
- **RCL** (SHIFT RCL on ABC) points at a cell on the grid, with ↑ ↓ and 8 4 6 2.
  `.` starts a block. ENTER types its name, such as `D2` or `D2:D4`.

A formula that can't be read isn't stored. The entry stays on the top line with
its cursor where reading stopped.

## Formulas

Excel's, so the CSV files work there too:

- `+ - * / ^ %`, comparisons `= <> < > <= >=` (1 or 0), parentheses.
- Names `A1`, `$A$1`, blocks `A1:B5`.
- As in Excel, `-2^2` is 4 and `2^3^2` is 64.
- Functions: SUM AVERAGE MIN MAX COUNT PRODUCT STDEV AND OR IF, ABS SQRT EXP
  LN LOG LOG10, SIN COS TAN ASIN ACOS ATAN ATAN2 SINH COSH TANH, PI RADIANS
  DEGREES, INT TRUNC ROUND MOD POWER FACT GAMMA SIGN NOT RAND. Angles are in
  radians, as in Excel: `=SIN(RADIANS(30))` is 0.5.
- SUM and the others skip text and empty cells in a block.
- IF only works out the branch it takes, so `=IF(A1=0,0,1/A1)` never divides
  by zero.

Errors show in the cell:

| Error | Meaning |
|---|---|
| `#DIV/0!` | Division by zero. |
| `#VALUE!` | Text where a number goes, or a block alone. |
| `#REF!` | Past Z or row 999, or a copy that moved off the sheet. |
| `#NAME?` | No such function. |
| `#NUM!` | No answer, or too big. |
| `#CIRC!` | The cell needs itself. |

Formulas can refer to each other in any order. A long chain is worked out in
passes, not by recursion, so it can't overflow the calculator's stack.

## Files

- Sheets are `.CSV` files in `BIN/APPS/SPREADSHEET` on the calculator's drive.
- Each cell is saved as typed, with formulas starting with `=`.
- Opening reads comma, semicolon or tab files. In a semicolon file it also reads
  decimal commas such as `1,5`.
- Up to 1200 cells fit. Cells past Z or row 999 are left out, with a message.
- The app remembers the last sheet, the format and the column widths.
- If the Sleep Timer or an empty battery ends the app with changes not saved,
  it keeps them in `RECOVER.TMP` and brings them back the next time it starts.

## Build, try, install

| VS Code task | Command | What it does |
|---|---|---|
| Build | `make` | `build/SPREADSHEET.bin` |
| Test | `make test` | The engine on the Mac (`tests/test_sheet.c`): formulas, errors, number display, copying, CSV. |
| Simulate | `make sim` | Opens `sim/drive/.../BUDGET.CSV`, types a formula, opens the catalog and points. Screenshots are saved in `build/sim-out`. |
| Simulate live | `make live` | In the browser, with a keypad. |
| Install | `make install` | Copies the `.bin` to the calculator in USB Drive mode. |

The simulator's decimals are doubles (`sim/sim_dec.c`), so on the Mac results
agree with the calculator's to about 15 digits.

## The code

| File | What it holds |
|---|---|
| `src/sheet.c`, `src/sheet.h` | The engine: cells, formulas, recalculation, how a value is shown, CSV. It has no screen or key code, so `make test` runs it on the Mac. |
| `src/app.c` | The screen, the keys, the menus and the files. |
| `px42f/`, `sim/` | The framework, kept the same as the template's by `../PX-42F-App-Template/tools/sync_framework.sh`. |
