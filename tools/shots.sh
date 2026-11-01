#!/bin/bash
# Every screenshot the manual uses, from the simulator, in build/shots:
# a/ the budget (opening, typing, the catalog, pointing, menus), b/ the
# savings (copy and fill). Then the manual (spreadsheet_manual.html).
#
#   tools/shots.sh
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
S="$P/build/shots"
cd "$P"
make build/sim >/dev/null
rm -rf "$S"; mkdir -p "$S"
fresh() { rm -rf build/sim-drive && mkdir -p build/sim-drive && cp -R sim/drive/. build/sim-drive/; }
fresh
build/sim "$S/a" build/sim-drive 10 shot:empty XEQ 10 shot:file DOWN DOWN ENTER 10 shot:open ENTER 10 shot:budget \
  R0C1 R0C1 R0C1 R0C1 DOWN R1C0 R0C3 R5C2 R3C4 R0C2 R5C2 10 shot:typing ENTER 10 shot:formula \
  DOWN DOWN DOWN R1C0 XEQ 10 shot:catalog ENTER R1C1 R4C1 UP UP UP UP R6C2 DOWN DOWN 10 shot:point \
  ENTER R1C4 10 shot:sum ENTER 10 shot:summed \
  R1C0 R5C1 R6C4 R4C4 R5C2 ENTER 10 shot:bad ON \
  R0C2 R0C0 R5C0 R5C1 R5C0 R5C2 10 shot:goto ENTER R5C0 R2C0 R2C2 R2C3 R4C1 R0C4 R3C4 10 shot:abc ENTER \
  SHIFT 10 shot:shift_menu R0C0 UP UP UP 10 shot:width ENTER SHIFT R0C1 10 shot:format DOWN DOWN DOWN ENTER 10 shot:fix2 \
  SHIFT R0C5 10 shot:help ON ON 10 shot:save_question ENTER >/dev/null
fresh
build/sim "$S/b" build/sim-drive XEQ DOWN DOWN ENTER DOWN ENTER 10 shot:savings \
  DOWN DOWN DOWN DOWN R0C3 R0C1 10 shot:marking R0C3 10 shot:copied \
  R0C0 DOWN R0C3 DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN 10 shot:marked R0C4 10 shot:filled \
  SHIFT R0C1 DOWN DOWN DOWN ENTER 10 shot:fixed QUIT >/dev/null
python3 "$P/tools/make_manual.py"
