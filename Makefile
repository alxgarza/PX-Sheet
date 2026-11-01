# A PX-42F app or game. Change these two for your own:
NAME := SPREADSHEET# the file: build/SPREADSHEET.bin, shown in the menu as SPREADSHEET
KIND := APPS# GAMES or APPS: the folder it goes in, /BIN/GAMES or /BIN/APPS

# The simulator's key script (sim/sim.c): keys, waits in ms, shot:NAME.
# Opens BUDGET.CSV (sim/drive), types a formula, then the catalog, pointing, menus.
SIM_SCRIPT := 10 shot:start XEQ 10 shot:file DOWN DOWN ENTER 10 shot:open ENTER 10 shot:budget \
              R0C1 R0C1 R0C1 R0C1 DOWN R1C0 R0C3 R5C2 R3C4 R0C2 R5C2 10 shot:typing ENTER 10 shot:formula \
              DOWN DOWN DOWN R1C0 XEQ 10 shot:catalog ENTER R1C1 R4C1 UP UP UP UP R6C2 DOWN DOWN 10 shot:point \
              ENTER R1C4 10 shot:sum ENTER 10 shot:summed \
              SHIFT 10 shot:shift_menu R0C1 10 shot:format DOWN DOWN DOWN ENTER 10 shot:fix2 \
              SHIFT R0C5 10 shot:help ON ON 10 shot:save_question ON

PICO_TOOLCHAIN ?= $(HOME)/.pico-sdk/toolchain/15_2_Rel1
DRIVE ?= /Volumes/PX-42F

CC := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-gcc
OBJCOPY := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-objcopy
NM := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-nm
SIZE := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-size

SRC := $(wildcard src/*.c)
HEADERS := $(wildcard src/*.h) $(wildcard px42f/*.h)

# The firmware's: Cortex-M33, Thumb, floats in integer registers (softfp).
ARCH := -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp
CFLAGS := $(ARCH) -std=c11 -Os -Wall -Wextra -ffunction-sections -fdata-sections \
          -fno-unwind-tables -fno-asynchronous-unwind-tables -Ipx42f -Isrc
LDFLAGS := $(ARCH) -T px42f/app.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,--orphan-handling=error -Wl,--no-warn-rwx-segments -Wl,-Map=build/$(NAME).map

.PHONY: all sim live test install clean

all: build/$(NAME).bin

build/$(NAME).elf: $(SRC) px42f/runtime.c px42f/app.ld $(HEADERS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) px42f/runtime.c -o $@
	$(SIZE) $@

build/$(NAME).bin: build/$(NAME).elf px42f/pxbin.py
	$(OBJCOPY) -O binary -j .image $< build/$(NAME).img
	$(NM) $< > build/$(NAME).sym
	python3 px42f/pxbin.py build/$(NAME).img build/$(NAME).sym $@

# The simulator: the same app code on the Mac. Screenshots in build/sim-out;
# the drive is a copy of sim/drive, in build/sim-drive.
build/sim: $(SRC) $(HEADERS) $(wildcard sim/*.c sim/*.h sim/fonts/*) | build
	cc -std=c11 -g -Wall -Wextra -fsanitize=address,undefined -Ipx42f -Isrc -Isim \
	   -DSIM_APP_DIR='"/BIN/$(KIND)/$(NAME)"' \
	   $(SRC) sim/*.c sim/fonts/*.c -o $@

sim: build/sim
	rm -rf build/sim-out build/sim-drive
	mkdir -p build/sim-drive && cp -R sim/drive/. build/sim-drive/
	build/sim build/sim-out build/sim-drive $(SIM_SCRIPT)

# The live simulator: the same build, in a web page with keys you can press.
# Its Restart button rebuilds the app and starts it again.
live: build/sim
	python3 sim/live.py build/sim sim/drive build/sim-drive "$(MAKE) build/sim"

# The sheet's engine on the computer (tests/test_sheet.c), with the simulator's decimals.
test: | build
	cc -std=c11 -g -Wall -Wextra -fsanitize=address,undefined -Ipx42f -Isrc -Isim \
	   tests/test_sheet.c src/sheet.c sim/sim_dec.c -o build/test_sheet
	build/test_sheet

# Onto the calculator's drive: Settings -> USB Drive -> Connect to Computer.
install: build/$(NAME).bin
	@test -d "$(DRIVE)/BIN/$(KIND)" || { echo "$(DRIVE)/BIN/$(KIND) not found: connect the PX-42F as a USB drive"; exit 1; }
	cp build/$(NAME).bin "$(DRIVE)/BIN/$(KIND)/"
	@echo "Copied. Eject the drive, then Settings -> Games & Apps."

build:
	mkdir -p build

clean:
	rm -rf build
