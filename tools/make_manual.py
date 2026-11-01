#!/usr/bin/env python3
"""Builds spreadsheet_manual.html, the Spreadsheet's manual: one HTML file
with its pictures inside. The two keyboard maps come from src/app.c's own
tables and the functions from src/sheet.c's, so the manual says what the
keys type and what the app has; the screenshots are the simulator's
(tools/shots.sh makes them, in build/shots, then runs this); the style is
tools/manual.css.

    tools/shots.sh
"""
import base64
import html
import os
import re

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(PROJ, 'build', 'shots')

app = open(os.path.join(PROJ, 'src', 'app.c')).read()
sheet = open(os.path.join(PROJ, 'src', 'sheet.c')).read()
style = open(os.path.join(PROJ, 'tools', 'manual.css')).read().strip()


def c_char_table(name):
    body = re.search(name + r'\[7\]\[6\] = \{(.*?)\};', app, re.S).group(1)
    table = []
    for row in re.findall(r'\{([^{}]*)\}', body):
        cells = []
        for tok in re.findall(r"'(?:\\.|[^'])'|(?<![\w'])0(?![\w'])", row):
            cells.append(None if tok == '0' else tok[1:-1])
        table.append(cells)
    assert len(table) == 7 and all(len(r) == 6 for r in table), name
    return table


LETTERS = c_char_table('letters')
SYMBOLS = c_char_table('symbols')
FUNCTIONS = re.findall(r'\{"(\w+)", "([^"]*)", "([^"]*)"\}', sheet)
assert len(FUNCTIONS) > 30

# What's printed on the calculator's keys, by place.
PRINTED = [
    ['&Sigma;+', '1/x', '&radic;x', 'LOG', 'LN', 'XEQ'],
    ['STO', 'RCL', 'R&darr;', 'SIN', 'COS', 'TAN'],
    ['ENTER', 'x&#8823;y', '+/&minus;', 'E', '&larr;'],
    ['&#9650;', '7', '8', '9', '&divide;'],
    ['&#9660;', '4', '5', '6', '&times;'],
    ['SHIFT', '1', '2', '3', '&minus;'],
    ['ON', '0', '.', 'R/S', '+'],
]
# The keys that do something else than type, while typing.
SPECIAL = {(2, 0): 'stores; SHIFT: ALPHA', (2, 4): 'deletes', (3, 0): 'cursor left',
           (4, 0): 'cursor right', (5, 0): 'the other keyboard, once', (6, 0): 'EXIT: drops it'}


def keyboard(abc, label):
    out = ['<div class="kbd-map" role="img" aria-label="%s">' % label]
    for r, row in enumerate(PRINTED):
        for c, printed in enumerate(row):
            cls = 's5' if r < 2 else 's6'
            letter, symbol = LETTERS[r][c], SYMBOLS[r][c] or LETTERS[r][c]
            note = ''
            if (r, c) == (0, 5):
                big, note = (letter, 'SHIFT: functions') if abc else ('XEQ', 'functions')
            elif (r, c) == (1, 1):
                big, note = (letter, 'SHIFT: a cell') if abc else ('RCL', 'points at a cell')
            else:
                ch = letter if abc else symbol
                if ch is None:
                    big, note = printed, SPECIAL.get((r, c), '')
                    cls += ' fnk'
                else:
                    big = html.escape('space' if ch == ' ' else ch)
                    other = symbol if abc else letter
                    if other and other != ch:
                        note = 'SHIFT: ' + html.escape('space' if other == ' ' else other)
            out.append('<div class="k %s"><b>%s</b><span>%s</span>%s</div>' % (
                cls, big, printed if big != printed else '', '<i>%s</i>' % note if note else ''))
    out.append('</div>')
    return '\n'.join(out)


def functions_table():
    rows = []
    for name, args, help_ in FUNCTIONS:
        rows.append('<tr><td><code>%s(%s)</code></td><td>%s</td></tr>' % (name, html.escape(args), html.escape(help_)))
    return '\n'.join(rows)


def img(name, alt):
    data = base64.b64encode(open(os.path.join(SHOTS, name), 'rb').read()).decode()
    return '<img class="shot" src="data:image/png;base64,%s" alt="%s">' % (data, html.escape(alt))


extra_css = """
.kbd-map .fnk { background: #4a4b52; }
.kbd-map .fnk b { font-size: 0.8rem; }
table.ref td:first-child { white-space: nowrap; }
"""

page = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PX-42F Spreadsheet</title>
STYLE
</head>
<body>
<main>

<header class="cover">
<h1>PX-42F Spreadsheet</h1>
<p>A spreadsheet for the PX-42F calculator, with Excel's formulas worked out in the calculator's own 34-digit decimals, and sheets saved as CSV files.</p>
</header>

<nav class="toc">
<h2>Contents</h2>
<ol>
  <li><a href="#intro">What it is</a></li>
  <li><a href="#start">Getting started</a></li>
  <li><a href="#screen">The screen</a></li>
  <li><a href="#moving">Moving around</a></li>
  <li><a href="#typing">Typing</a></li>
  <li><a href="#formulas">Formulas</a></li>
  <li><a href="#functions">Functions</a></li>
  <li><a href="#copy">Copying and filling</a></li>
  <li><a href="#look">Number format and widths</a></li>
  <li><a href="#files">Files</a></li>
  <li><a href="#limits">Limits</a></li>
  <li><a href="#reference">Quick reference</a></li>
</ol>
</nav>

<!-- ============================================================ -->
<h2 id="intro">1. What it is</h2>

<p>The Spreadsheet turns the PX-42F into a pocket spreadsheet: a grid of cells, A1 to Z999, where each cell holds a number, some text, or a formula that works something out from other cells. Change a number and every formula that depends on it changes too.</p>
<ul>
  <li><strong>The calculator's numbers.</strong> Formulas are worked out in 34-digit decimals, from the same library Plus42 computes with. <code>=0.1+0.2</code> is exactly 0.3.</li>
  <li><strong>Excel's formulas.</strong> <code>=SUM(B2:B9)</code>, <code>=IF(A1&gt;0,A1,0)</code>, <code>$A$1</code> and the rest work as in Excel, with the same function names.</li>
  <li><strong>CSV files.</strong> Sheets are saved as CSV files on the calculator's drive. Make one on a computer, work on it on the calculator, and open it again in Excel or Numbers with its formulas still working.</li>
</ul>

<div class="shots">
<figure>IMG:a/budget.png|A budget: items, costs, quantities and totals|<figcaption>A budget sheet.</figcaption></figure>
<figure>IMG:a/catalog.png|The function catalog|<figcaption>The function catalog, on <kbd>XEQ</kbd>.</figcaption></figure>
<figure>IMG:b/fixed.png|Savings growing year by year, at 2 decimals|<figcaption>One row of formulas, filled down.</figcaption></figure>
</div>

<!-- ============================================================ -->
<h2 id="start">2. Getting started</h2>

<h3>Installing</h3>
<ol>
  <li>The calculator needs the PX-42F-Plus firmware with app API version 4 or later.</li>
  <li>On the calculator, open <span class="mono">Settings &rarr; USB Drive &rarr; Connect to Computer</span>.</li>
  <li>Copy <code>SPREADSHEET.bin</code> into <code>BIN/APPS</code> on the drive named <code>PX-42F</code>. From the project folder, <code>make install</code> does it.</li>
  <li>To try the examples, copy <code>BUDGET.CSV</code> and <code>SAVINGS.CSV</code> (in the project's <code>sim/drive/BIN/APPS/SPREADSHEET/</code>) into <code>BIN/APPS/SPREADSHEET</code> on the drive. Make the folder if it isn't there.</li>
  <li>Eject the drive, then press <kbd>EXIT</kbd> on the calculator.</li>
</ol>

<h3>Starting and leaving</h3>
<p>Start it from <span class="mono">Settings &rarr; Games &amp; Apps &rarr; Apps &rarr; SPREADSHEET</span>. It opens the sheet you had last, or an empty one called SHEET1 the first time.</p>
<p>To leave, press <kbd>EXIT</kbd> (the <kbd>ON</kbd> key). If the sheet has changes that aren't saved, it asks first:</p>
<figure>IMG:a/save_question.png|Save changes to BUDGET?|<figcaption><kbd>ENTER</kbd> saves, <kbd>&larr;</kbd> leaves without saving, <kbd>EXIT</kbd> goes back to the sheet.</figcaption></figure>
<p>If the Sleep Timer or an empty battery ends the app before you've saved, there's no time to ask. The changes are kept anyway, and the sheet comes back as you left it the next time you start the app. It says so on the bottom line; save it then to keep it.</p>

<!-- ============================================================ -->
<h2 id="screen">3. The screen</h2>

<figure>IMG:a/summed.png|The budget with a SUM in E6|<figcaption>The cursor on E7, below the SUM just entered in E6.</figcaption></figure>
<ul>
  <li><strong>The top line</strong> shows the cursor's cell, such as <code>E7</code>, and what's typed in it: the formula itself, not its result. While you type, it shows what you're typing, with a cursor.</li>
  <li><strong>The grid</strong> shows 10 rows and as many columns as fit across. The column letters are along the top and the row numbers down the left; the cursor's column and row are highlighted. Numbers are right-aligned and text left-aligned. A number too wide for its column shows as <code>####</code>; make the column wider (<a href="#look">section 9</a>).</li>
  <li><strong>The bottom line</strong> shows the menu for the top row of keys. While you type it shows what the keys do, and after some actions a short message, until the next key.</li>
</ul>

<!-- ============================================================ -->
<h2 id="moving">4. Moving around</h2>

<p>As in the HP-42S's matrix editor, <kbd>&#9650;</kbd> <kbd>&#9660;</kbd> move up and down, and the first two keys of the menu, <kbd>&larr;</kbd> and <kbd>&rarr;</kbd> on the screen, move left and right. Held down, they repeat. The grid scrolls to keep the cursor in view.</p>
<div class="table-wrap"><table>
<thead><tr><th>Key</th><th>Does</th></tr></thead>
<tbody>
<tr><td><kbd>&#9650;</kbd> <kbd>&#9660;</kbd></td><td>Up or down a row.</td></tr>
<tr><td><kbd>SHIFT</kbd> <kbd>&#9650;</kbd> <kbd>&#9660;</kbd></td><td>Up or down ten rows.</td></tr>
<tr><td><kbd>&larr;</kbd> <kbd>&rarr;</kbd> (menu)</td><td>Left or right a column.</td></tr>
<tr><td>GOTO (menu)</td><td>Goes to a cell by name: type it, such as <code>C12</code>, then <kbd>ENTER</kbd>.</td></tr>
<tr><td><kbd>ENTER</kbd></td><td>Edits what's in the cell, on the top line.</td></tr>
<tr><td><kbd>&larr;</kbd> (backspace)</td><td>Empties the cell.</td></tr>
<tr><td><kbd>EXIT</kbd></td><td>Leaves the app, asking first if there are changes to save.</td></tr>
</tbody>
</table></div>

<h3>The menus</h3>
<p>The top row of keys works the menu on the bottom line. <kbd>SHIFT</kbd> shows the second menu, for one key.</p>
<div class="shots">
<figure>IMG:a/budget.png|The menu: left, right, GOTO, COPY, PASTE, FILE|<figcaption>&larr; &rarr; GOTO COPY PASTE FILE</figcaption></figure>
<figure>IMG:a/shift_menu.png|The SHIFT menu: WIDTH, FORMAT, CLEAR, CALC, HELP|<figcaption><kbd>SHIFT</kbd>: WIDTH FORMAT CLEAR CALC HELP</figcaption></figure>
</div>
<div class="table-wrap"><table>
<thead><tr><th>Menu key</th><th>Does</th></tr></thead>
<tbody>
<tr><td>GOTO</td><td>Goes to a cell by name.</td></tr>
<tr><td>COPY</td><td>Marks a block to copy; COPY again copies it (<a href="#copy">section 8</a>).</td></tr>
<tr><td>PASTE</td><td>Puts what was copied at the cursor, or fills a marked block with it.</td></tr>
<tr><td>FILE</td><td>Save, Save as, Open, New sheet (<a href="#files">section 10</a>).</td></tr>
<tr><td>WIDTH</td><td>The cursor's column wider or narrower, with <kbd>&#9650;</kbd> <kbd>&#9660;</kbd>.</td></tr>
<tr><td>FORMAT</td><td>How numbers are shown: General, Fix or Sci.</td></tr>
<tr><td>CLEAR</td><td>Empties the whole sheet, after asking.</td></tr>
<tr><td>CALC</td><td>Works every formula out again. Only <code>RAND()</code> gives a different answer.</td></tr>
<tr><td>HELP</td><td>A page of the keys, on the calculator.</td></tr>
</tbody>
</table></div>

<!-- ============================================================ -->
<h2 id="typing">5. Typing</h2>

<h3>Starting</h3>
<p>On the sheet, just start typing. A digit, <kbd>.</kbd> or an operator starts a new entry in the cursor's cell, in place of what was there:</p>
<ul>
  <li><strong>A number:</strong> type it. <kbd>E</kbd> types the exponent: <code>6.02E23</code>.</li>
  <li><strong>A formula:</strong> <kbd>STO</kbd> types the <code>=</code> it starts with. <kbd>RCL</kbd> starts one and points at a cell straight away (below).</li>
  <li><strong>Text:</strong> press ALPHA (<kbd>SHIFT</kbd> <kbd>ENTER</kbd>) first, for the letters.</li>
  <li><strong>To change what's there:</strong> <kbd>ENTER</kbd> puts the cell's entry on the top line to edit.</li>
</ul>
<p><kbd>ENTER</kbd> stores the entry and moves the cursor down a row, ready for the next. <kbd>EXIT</kbd> drops it and leaves the cell as it was.</p>
<div class="shots">
<figure>IMG:a/typing.png|=D2/C typed on the top line|<figcaption>Typing <code>=D2/C2</code> into E2: <kbd>STO</kbd> <kbd>LOG</kbd> <kbd>2</kbd> <kbd>&divide;</kbd> <kbd>&radic;x</kbd>&hellip;</figcaption></figure>
<figure>IMG:a/formula.png|E2 shows 2.5 after ENTER|<figcaption>&hellip; <kbd>2</kbd> <kbd>ENTER</kbd>: E2 shows 2.5, and the cursor is on E3.</figcaption></figure>
</div>

<h3>Two keyboards</h3>
<p>While you type, the keys work as one of two keyboards. ALPHA (<kbd>SHIFT</kbd> <kbd>ENTER</kbd>) switches between them, and the bottom line says which is on: <code>123</code> or <code>ABC</code>. <kbd>SHIFT</kbd> before a key types its character on the other keyboard, once.</p>
<p><strong>123</strong> is for numbers and formulas. The digits and <code>+ &minus; &times; &divide;</code> (as <code>* /</code>) are where they're printed, and the keys around them type what formulas need. The top row types the column letters A to E, so <code>=A1+B1</code> needs no <kbd>SHIFT</kbd>.</p>
KEYS_123
<p><strong>ABC</strong> is for text. Each key types the letter printed on it, as in the calculator's ALPHA keyboard.</p>
KEYS_ABC
<div class="shots">
<figure>IMG:a/abc.png|NOTES typed on the ABC keyboard|<figcaption>ALPHA, then <kbd>+/&minus;</kbd> <kbd>E</kbd> <kbd>4</kbd> <kbd>LN</kbd> <kbd>&divide;</kbd>: NOTES.</figcaption></figure>
<figure>IMG:a/goto.png|GOTO with A12 typed|<figcaption>GOTO types on ABC: <kbd>&Sigma;+</kbd> <kbd>SHIFT</kbd> <kbd>1</kbd> <kbd>SHIFT</kbd> <kbd>2</kbd> is A12.</figcaption></figure>
</div>

<h3>The other keys</h3>
<div class="table-wrap"><table>
<thead><tr><th>Key</th><th>Does, while typing</th></tr></thead>
<tbody>
<tr><td><kbd>&#9650;</kbd> <kbd>&#9660;</kbd></td><td>Move the cursor left and right in the entry. With <kbd>SHIFT</kbd>: to its start or end.</td></tr>
<tr><td><kbd>&larr;</kbd></td><td>Deletes the character before the cursor. <kbd>SHIFT</kbd> <kbd>&larr;</kbd> clears the entry.</td></tr>
<tr><td><kbd>XEQ</kbd></td><td>The function catalog (on ABC: <kbd>SHIFT</kbd> <kbd>XEQ</kbd>).</td></tr>
<tr><td><kbd>RCL</kbd></td><td>Points at a cell or a block (on ABC: <kbd>SHIFT</kbd> <kbd>RCL</kbd>).</td></tr>
<tr><td><kbd>ENTER</kbd></td><td>Stores the entry. <kbd>SHIFT</kbd> <kbd>ENTER</kbd> (ALPHA): the other keyboard.</td></tr>
<tr><td><kbd>EXIT</kbd></td><td>Drops the entry.</td></tr>
</tbody>
</table></div>

<h3>The function catalog: XEQ</h3>
<p><kbd>XEQ</kbd> lists every function, with what it takes and what it gives. <kbd>&#9650;</kbd> <kbd>&#9660;</kbd> move through the list (with <kbd>SHIFT</kbd>, a page at a time), and a letter key jumps to the first function starting with that letter. <kbd>ENTER</kbd> types the function's name and its <code>(</code> at the cursor; <kbd>EXIT</kbd> goes back without typing. The list opens where you last picked from it.</p>

<h3>Pointing at cells: RCL</h3>
<p>Instead of typing a cell's name, point at it. <kbd>RCL</kbd> puts a highlight on the grid; move it with <kbd>&#9650;</kbd> <kbd>&#9660;</kbd> or <kbd>8</kbd> <kbd>2</kbd> (up and down) and <kbd>4</kbd> <kbd>6</kbd> (left and right). <kbd>.</kbd> starts a block there: move to its other corner. <kbd>ENTER</kbd> types the cell's name, or the block's, such as <code>D2:D4</code>, and goes back to typing.</p>
<div class="shots">
<figure>IMG:a/point.png|D2 to D4 marked, =SUM( on the top line|<figcaption><code>=SUM(</code> from the catalog, then <kbd>RCL</kbd>, to D2, <kbd>.</kbd>, down to D4.</figcaption></figure>
<figure>IMG:a/sum.png|=SUM(D2:D4) on the top line|<figcaption><kbd>ENTER</kbd> typed <code>D2:D4</code>; <kbd>COS</kbd> types the <code>)</code>.</figcaption></figure>
</div>

<h3>When a formula can't be read</h3>
<p>A formula that can't be read, such as <code>=1+*2</code> or one with a <code>(</code> left open, isn't stored. The calculator sounds its error tone, the entry stays on the top line, and its cursor is where reading stopped, to fix it there.</p>
<figure>IMG:a/bad.png|=1+*2 with the cursor on the star|<figcaption>The cursor is on the <code>*</code>.</figcaption></figure>

<!-- ============================================================ -->
<h2 id="formulas">6. Formulas</h2>

<p>A formula starts with <code>=</code>. It's written as in Excel, so a sheet works the same there.</p>
<div class="table-wrap"><table>
<thead><tr><th>Part</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>12</code> <code>-3.5</code> <code>.25</code> <code>6.02E23</code></td><td>Numbers.</td></tr>
<tr><td><code>B2</code></td><td>A cell's value. An empty cell is 0; one with text gives <code>#VALUE!</code>.</td></tr>
<tr><td><code>B2:D9</code></td><td>A block, for functions such as SUM: from B2 to D9, row by row.</td></tr>
<tr><td><code>$B$2</code> <code>B$2</code> <code>$B2</code></td><td>The same cell; the <code>$</code> only matter when the formula is copied (<a href="#copy">section 8</a>).</td></tr>
<tr><td><code>( )</code></td><td>Grouping.</td></tr>
<tr><td><code>-</code> in front</td><td>Minus. As in Excel it comes before <code>^</code>: <code>=-2^2</code> is 4.</td></tr>
<tr><td><code>%</code> after</td><td>Divided by 100: <code>=8%</code> is 0.08.</td></tr>
<tr><td><code>^</code></td><td>Power, left to right as in Excel: <code>=2^3^2</code> is 64. It shows as &uarr; on the screen, the HP-42S's own character.</td></tr>
<tr><td><code>* /</code></td><td>Times, divided by.</td></tr>
<tr><td><code>+ -</code></td><td>Plus, minus.</td></tr>
<tr><td><code>= &lt;&gt; &lt; &gt; &lt;= &gt;=</code></td><td>Comparisons: 1 if true, 0 if false. <code>TRUE</code> and <code>FALSE</code> are 1 and 0.</td></tr>
</tbody>
</table></div>
<p>Letters can be typed in either case: <code>=sum(b2:b4)</code> is <code>=SUM(B2:B4)</code>. Spaces are allowed anywhere between the parts.</p>

<h3>Errors</h3>
<p>A formula that can't give an answer shows an error in its cell. A formula that uses that cell shows the same error.</p>
<div class="table-wrap"><table>
<thead><tr><th>Error</th><th>Meaning</th></tr></thead>
<tbody>
<tr><td><code>#DIV/0!</code></td><td>A division by zero, or the AVERAGE of nothing.</td></tr>
<tr><td><code>#VALUE!</code></td><td>Text where a number goes, or a block used as one number.</td></tr>
<tr><td><code>#REF!</code></td><td>A cell past Z or row 999, or a copy that moved a name off the sheet.</td></tr>
<tr><td><code>#NAME?</code></td><td>No function by that name.</td></tr>
<tr><td><code>#NUM!</code></td><td>No answer (<code>=SQRT(-1)</code>, <code>=0^0</code>) or too big.</td></tr>
<tr><td><code>#CIRC!</code></td><td>The cell needs its own value, directly or through other cells.</td></tr>
</tbody>
</table></div>

<h3>Working it out</h3>
<p>Every formula is worked out again after every change, in any order: a formula can use cells below or to the right of it, and chains of formulas hundreds of cells long work too.</p>

<!-- ============================================================ -->
<h2 id="functions">7. Functions</h2>

<p>The functions have Excel's names. Those marked <em>values</em> take any number of numbers, cells and blocks, separated by commas: <code>=SUM(B2:B9,D2,100)</code>. In a block they skip text and empty cells, as Excel does.</p>
<div class="table-wrap"><table class="ref">
<thead><tr><th>Function</th><th>Gives</th></tr></thead>
<tbody>
FUNCTIONS_TABLE
</tbody>
</table></div>
<ul>
  <li><strong>Angles are in radians</strong>, as in Excel. For degrees, convert: <code>=SIN(RADIANS(30))</code> is 0.5, <code>=DEGREES(ATAN(1))</code> is 45.</li>
  <li><strong>IF</strong> only works out the branch it takes, so <code>=IF(A1=0,0,1/A1)</code> never divides by zero. Without the third part it gives 0 when the test is 0.</li>
  <li><strong>ROUND</strong> and <strong>TRUNC</strong> take the digits after the point, 0 if left out; a negative count rounds to tens, hundreds: <code>=ROUND(1234.5,-2)</code> is 1200. ROUND rounds halves away from zero.</li>
  <li><strong>MOD</strong> has the sign of its second number, as in Excel and the HP-42S: <code>=MOD(-7,3)</code> is 2.</li>
  <li><strong>LOG</strong> is base 10, or the base given: <code>=LOG(8,2)</code> is 3.</li>
  <li><strong>STDEV</strong> is the sample standard deviation (n&minus;1), as Excel's STDEV.</li>
  <li><strong>RAND</strong> gives a new number every time the sheet is worked out again.</li>
</ul>

<!-- ============================================================ -->
<h2 id="copy">8. Copying and filling</h2>

<p>To copy a block: COPY marks it from the cursor's cell; move to its other corner, then press COPY again. The bottom line says how big a block was copied. PASTE then puts it with its top left at the cursor, in place of what was there.</p>
<p><strong>Names move with a copy.</strong> A formula copied one row down refers to the cells one row down: <code>=B4*2</code> copied from C4 to C5 becomes <code>=B5*2</code>. A name with <code>$</code> stays: <code>=B4*$B$1</code> becomes <code>=B5*$B$1</code>. <code>$B1</code> keeps its column and <code>B$1</code> its row. A name moved off the sheet becomes <code>#REF!</code>.</p>
<p><strong>Filling.</strong> Mark a block first (COPY, then move), then PASTE: what was copied is repeated to fill it. Copy one row of formulas, mark the rows below it, and PASTE fills them all, as Excel's fill down does.</p>
<div class="shots">
<figure>IMG:b/savings.png|Savings: a rate, a first year, and one row of formulas|<figcaption>SAVINGS.CSV: row 5 adds a year and the interest.</figcaption></figure>
<figure>IMG:b/marking.png|A5 to B5 marked|<figcaption>COPY, &rarr;: A5:B5 marked; COPY again copies it.</figcaption></figure>
<figure>IMG:b/marked.png|A6 to A14 marked|<figcaption>On A6, COPY and &#9660; eight times marks A6:A14.</figcaption></figure>
<figure>IMG:b/filled.png|Rows 6 to 14 filled with years and balances|<figcaption>PASTE fills A6:B14, each row from the one above.</figcaption></figure>
</div>
<p><strong>Emptying.</strong> <kbd>&larr;</kbd> empties the cursor's cell. With a block marked, <kbd>&larr;</kbd> or CLEAR in the menu empties the block. <kbd>EXIT</kbd> unmarks it.</p>

<!-- ============================================================ -->
<h2 id="look">9. Number format and widths</h2>

<p><strong>FORMAT</strong> (<kbd>SHIFT</kbd> menu) sets how numbers show, for the whole sheet:</p>
<ul>
  <li><strong>General</strong> (to start with): as many digits as the column has room for, up to 15, without zeros at the end. Very large or small numbers, or ones that don't fit, show as <code>1.5E-7</code>.</li>
  <li><strong>Fix 0 to Fix 6</strong>: that many digits after the point, as the HP-42S's FIX: 2.5 in Fix 2 is <code>2.50</code>.</li>
  <li><strong>Sci 1 to Sci 6</strong>: a digit, then that many after the point, then the exponent: <code>1.23E4</code>.</li>
</ul>
<p>Only the showing changes: every number keeps all its digits.</p>
<div class="shots">
<figure>IMG:a/format.png|The list of number formats|<figcaption>FORMAT.</figcaption></figure>
<figure>IMG:a/fix2.png|The budget in Fix 2|<figcaption>Fix 2.</figcaption></figure>
</div>
<p><strong>WIDTH</strong> (<kbd>SHIFT</kbd> menu) makes the cursor's column wider with <kbd>&#9650;</kbd> or narrower with <kbd>&#9660;</kbd>, from 3 to 30 characters; <kbd>ENTER</kbd> is done. Columns start 9 wide, and four fit on the screen.</p>
<figure>IMG:a/width.png|Column A made 12 wide|<figcaption>Column A, 12 wide.</figcaption></figure>
<p>The app remembers the format and the widths, with the sheet you were on.</p>

<!-- ============================================================ -->
<h2 id="files">10. Files</h2>

<p>Sheets are CSV files in <code>BIN/APPS/SPREADSHEET</code> on the calculator's drive, named <code>NAME.CSV</code>. FILE in the menu has:</p>
<div class="table-wrap"><table>
<thead><tr><th>Choice</th><th>Does</th></tr></thead>
<tbody>
<tr><td>Save NAME.CSV</td><td>Saves the sheet under its name.</td></tr>
<tr><td>Save as&hellip;</td><td>Saves it under a new name, typed on ABC: up to 20 letters, digits, <code>-</code> or <code>_</code>. From then on the sheet has that name.</td></tr>
<tr><td>Open&hellip;</td><td>Lists the <code>.CSV</code> files in the folder to open one. If the sheet has changes, it asks to save them first.</td></tr>
<tr><td>New sheet</td><td>An empty sheet, under a name you type. It asks about changes first.</td></tr>
</tbody>
</table></div>
<div class="shots">
<figure>IMG:a/file.png|The FILE choices|<figcaption>FILE.</figcaption></figure>
<figure>IMG:a/open.png|Open, listing BUDGET|<figcaption>Open&hellip;</figcaption></figure>
</div>

<h3>On a computer</h3>
<p>In USB Drive mode, the sheets are in <code>BIN/APPS/SPREADSHEET</code>. A CSV file is the sheet as text, a row on each line, the cells separated by commas.</p>
<ul>
  <li><strong>Saving</strong> writes each cell as you typed it, formulas with their <code>=</code>. Excel and Numbers open the file with the formulas working.</li>
  <li><strong>Opening</strong> reads files with commas, semicolons or tabs between the cells, such as Excel writes in different countries. In a semicolon file, numbers with a decimal comma (<code>1,5</code>) are read as numbers.</li>
  <li>A file saved by Excel holds the results, not the formulas: Excel's CSV doesn't keep them.</li>
  <li>Text with characters the calculator's fonts don't have, such as accented letters, shows <code>?</code> in their place. They are saved as they were.</li>
  <li>Text that looks like a number but should stay text, such as a part number <code>007</code>, starts with an apostrophe: <code>'007</code>. The apostrophe isn't shown.</li>
</ul>

<!-- ============================================================ -->
<h2 id="limits">11. Limits</h2>
<ul>
  <li>Cells A1 to Z999, up to 1200 of them holding something; an entry up to 120 characters. Opening a file with more columns or rows leaves the rest out, with a message.</li>
  <li>Numbers: 34 significant digits, up to about 10<sup>6144</sup>.</li>
  <li>Formulas: parentheses and functions inside each other up to 16 deep.</li>
  <li>No text in formulas: no <code>"quoted"</code> strings, and a formula's result is always a number.</li>
  <li>One number format for the whole sheet; no dates, no charts.</li>
  <li>The app runs in the calculator's 128 KB for apps; text in cells shares about 60 KB.</li>
</ul>

<!-- ============================================================ -->
<h2 id="reference">12. Quick reference</h2>
<p><kbd>SHIFT</kbd> HELP shows this on the calculator:</p>
<figure>IMG:a/help.png|The help page|<figcaption>HELP.</figcaption></figure>

<footer>PX-42F Spreadsheet. Plus42 and its decimal arithmetic are Thomas Okken's; Excel is Microsoft's.</footer>
</main>
</body>
</html>
'''

out = page.replace('STYLE', style.replace('</style>', extra_css + '</style>'))
out = out.replace('KEYS_123', keyboard(False, 'The 123 keyboard'))
out = out.replace('KEYS_ABC', keyboard(True, 'The ABC keyboard'))
out = out.replace('FUNCTIONS_TABLE', functions_table())
out = re.sub(r'IMG:([^|]+)\|([^|]+)\|', lambda m: img(m.group(1), m.group(2)), out)
path = os.path.join(PROJ, 'spreadsheet_manual.html')
open(path, 'w').write(out)
print('%s: %d bytes' % (os.path.relpath(path, PROJ), len(out)))
