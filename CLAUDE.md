# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RunCPM for the Raspberry Pi Pico / Pico 2 — a CP/M 2.2 emulator that runs on RP2040/RP2350 microcontrollers. It emulates a Z80 CPU, the CP/M BIOS/BDOS, and maps CP/M disk drives to subdirectories on an SD card accessed over SPI.

This is Guido Lehwalder's (GL) Pico-specific fork/adaptation of [MockbaTheBorg/RunCPM](https://github.com/MockbaTheBorg/RunCPM), compiled through the Arduino IDE with the earlephilhower `arduino-pico` core.

## Build & Flash

**Toolchain**: Arduino IDE with the [earlephilhower/arduino-pico](https://github.com/earlephilhower/arduino-pico) RP2040 board support package. There is no CMake or Makefile — compilation is IDE-driven.

**Required library**: `SdFat` by Greiman (install via Arduino Library Manager). Optionally use `ESP8266SdFat` (bundled with the RP2040 core) by swapping the `#include` in the `.ino`.

**Sketch locations** (one per target board):
- `GL20251130_Pico_Binary_Source_RunCPM_v6_9/RunCPM_v6_9_Pico_276Mhz_Internal_30112025/` — Raspberry Pi Pico (RP2040, 276 MHz)
- `GL20251130_Pico_Binary_Source_RunCPM_v6_9/RunCPM_v6_9_Pico2_300Mhz_Internal_30112025/` — Raspberry Pi Pico 2 (RP2350, 300 MHz)

**Pre-built binaries**: `.uf2` files at the top of `GL20251130_Pico_Binary_Source_RunCPM_v6_9/` can be flashed directly by holding BOOTSEL on the Pico while connecting USB, then copying the `.uf2` to the mass-storage device that appears.

**Speed optimization**: Replacing `-Os` with `-O3` in the arduino-pico `platform.txt` (`compiler.flags=` line) yields ~35% more emulated Z80 speed at the same clock rate. See `Pico_speed_O3_compile.txt` for the exact flag and memory-usage figures.

**Suppressing warnings**: Add `compiler.cpp_warning_flags=-Wno-register -Werror=return-type` to `platform.txt` and update `compiler.cpp.flags` to reference it (see README.md for the exact snippet). Also comment out the `#warning` in `SdFat/src/SdFat.h` about `File32`.

**Serial monitor**: 115200 baud (`#define SERIALSPD 115200` in the `.ino`). The board blinks its LED while waiting for the USB serial host to connect before booting CP/M.

## Repository Layout

```
GL20251130_Pico_Binary_Source_RunCPM_v6_9/
├── RunCPM_v6_9_Pico_276Mhz_Internal_30112025/   ← Pico sketch + all .h sources
├── RunCPM_v6_9_Pico2_300Mhz_Internal_30112025/  ← Pico 2 sketch + all .h sources
├── CCP/          ← External CCP binary images (DR, CCPZ, ZCP2, ZCP3, Z80) in 60K/64K variants
├── ABDOS/        ← Alternate BDOS (BDOS.SYS + cpm_abdos.h) by Pavel Zampach
├── SDCARD/       ← Reference SD card content (CCP file for external CCP mode)
├── *.uf2         ← Pre-compiled flash images
└── ABDOS_SYS.TXT
SDCard_content.zip   ← Archive for preparing the SD card
Pico_speed_O3_compile.txt
README.md
```

Both sketch directories are structurally identical; the Pico 2 variant differs only in board clock speed (300 MHz vs 276 MHz) and the boot message.

## Architecture

All logic lives in `.h` files that are `#include`d into the single Arduino `.ino` sketch. The sketch provides `setup()` (boots CP/M, then loops in the CP/M run loop) and `loop()` (LED blink when CP/M has exited).

| File | Role |
|---|---|
| `globals.h` | All compile-time `#define` configuration switches — CPU model, CCP choice, TPA size, debug options, memory layout constants, global variable declarations |
| `hardware/pico/pico_sd_spi.h` | Board-specific pin assignments, LED GPIO, `SdFat SD` instance, board `#define`s |
| `abstraction_arduino.h` | Platform abstraction layer: `HostOS`, `_RamLoad()` (load file from SD into emulated RAM), filesystem helpers, `FOLDERCHAR`, CP/M FCB structs |
| `ram.h` | 64 KB emulated Z80 RAM array; `_RamRead` / `_RamWrite` / `_RamRead16` / `_RamWrite16` / `_RamSysAddr` — all become direct array macros when `RAM_FAST` is defined (single-bank mode) |
| `console.h` | Serial console I/O: `_putcon`, `_puts`, `_puthex8/16`, `_getcon`, `_kbhit`, XMODEM-related 7/8-bit masking |
| `cpu1.h` – `cpu4.h` | Four Z80 emulator implementations with different speed/code-size tradeoffs. Selected by `#define CPU "cpu1.h"` in `globals.h`. Each defines `Z80reset()`, `Z80run()`, `Z80debug()`, `Z80estimateClock()`, and all Z80 register variables |
| `cpu_mhz.h` | Estimates and prints the effective emulated Z80 MHz at startup |
| `disk.h` | CP/M BDOS disk operations — maps CP/M drives (A:–P:) and user areas (0–15 / A–F) to SD card subdirectories; implements `_Bdos()` |
| `host.h` | Thin `hostbdos()` hook (currently a stub returning 0) |
| `cpm.h` | BIOS (`_Bios()`) and BDOS function dispatch; enumerations `eBIOSFunc` / `eBDOSFunc`; CP/M memory map patching (`_PatchCPM`, `_PatchBIOS`) |
| `ccp.h` | Internal CCP (v3.3) implementation — included only when `#define CCP_INTERNAL` is set |
| `debug.h` | Interactive Z80 debugger (activated by `#define DEBUG` or at runtime via `DEBUGKEY`) |
| `resource.h` | Embedded binary resources |

### Key Configuration (`globals.h`)

| `#define` | Effect |
|---|---|
| `CPU "cpu1.h"` | Z80 CPU model (1 = larger/original, 2–4 = smaller/faster variants) |
| `CPU_SPEED 0` | 0 = run at full speed; higher values throttle to simulate a slower Z80 |
| `CCP_INTERNAL` | Use built-in CCP (no file needed on SD). Alternatives: `CCP_DR`, `CCP_CCPZ`, `CCP_ZCPR2`, `CCP_ZCPR3`, `CCP_Z80` |
| `TPASIZE 64` | Transient Program Area size in KB (60 = strict CP/M 2.2, 64 = extended) |
| `BANKS 1` | Single bank enables `RAM_FAST` (direct array macros, no function call overhead) |
| `INT_HANDOFF` | Use RST/interrupt for BIOS/BDOS handoff instead of legacy `IN`/`OUT` port method |
| `ABDOS` | Use alternate BDOS.SYS (requires `BDOS.SYS` on A: user 0; incompatible with `CCP_INTERNAL`) |
| `DEBUG` / `DEBUGONHALT` | Enable interactive Z80 debugger |
| `DEBUGLOG` | Write call-trace log to `RunCPM.log` on SD |
| `USE_PUN` / `USE_LST` | Enable punch (`pun.txt`) and list (`lst.txt`) virtual devices |
| `NOSLASH` | Translate `/` → `_` in filenames to avoid path separator conflicts |

### Board Variant Selection (`.ino`)

The active board is chosen by commenting/uncommenting the `#include "hardware/pico/..."` line near the top of the `.ino`:
- `pico_sd_spi.h` — standard Pico (GPIO 25 LED)
- `pico_w_sd_spi.h` — Pico W (GPIO 64 LED)
- `pico_sd_rc2040_spi.h` — RC2040 board (different SPI pin assignments)
- Additional boards in `hardware/arduino/`, `hardware/esp32/`, `hardware/stm32/`, `hardware/teensy/`

### SD Card Structure

The SD card must be FAT16 or FAT32 formatted. Drives and user areas map to subdirectories:

```
(SD root)/
├── A/
│   ├── 0/      ← Drive A:, User 0 (required; put CP/M programs here)
│   ├── 1/      ← Drive A:, User 1 (created automatically when selected)
│   └── ...
├── B/
│   └── 0/
└── ...
```

- Drive letters `A`–`P` only (CP/M limit of 16 drives)
- User areas `0`–`9` and `A`–`F` (for user areas 10–15)
- All filenames and folder names **must be UPPERCASE**
- When using an external CCP (not `CCP_INTERNAL`), the CCP binary (`CCP-DR.64K` etc.) must be at the SD root
- `AUTOEXEC.TXT` at SD root is executed on first boot if present

### SPI Wiring (Standard Pico)

```
MISO  GPIO 16  (Pin 21)
CS    GPIO 17  (Pin 22)
SCK   GPIO 18  (Pin 24)
MOSI  GPIO 19  (Pin 25)
```

SD card modules with onboard 5V→3.3V converters should be powered from the 5V pin, not the Pico's 3.3V rail.
