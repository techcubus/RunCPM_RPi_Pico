#!/usr/bin/env python3
"""
make_modem_com.py — generate MODEM.COM for RunCPM Pico W modem testing.

MODEM.COM is a 95-byte CP/M program that bridges the CP/M console (ttyACM0)
to the RunCPM virtual WiFi modem via standard BIOS AUX calls:
  B_AUXOUT  (BIOS+18)  send byte to modem
  B_READER  (BIOS+21)  receive byte from modem
  B_AUXIST  (BIOS+54)  AUX input status

Usage:
  python3 make_modem_com.py [output_path]

Output defaults to MODEM.COM in the current directory.
Copy MODEM.COM to A/0/ on the SD card (filename must be uppercase).
"""

import sys

# Hand-assembled from MODEM.ASM (ORG 0100H)
# Addresses:
#   0100 START      0116 MAIN_LOP  0127 CHK_CON
#   013C AUXIST_F   0144 READER_F  014C AUXOUT_A
#   0157 BIOSBASE   0159 ATZSTR
CODE = bytes([
    # 0100: get BIOS base from warm-boot vector at 0001H
    0x2A, 0x01, 0x00,   # LD HL,(0001H)
    0x2B,               # DEC HL
    0x2B,               # DEC HL
    0x2B,               # DEC HL          ; HL = BIOS base
    0x22, 0x57, 0x01,   # LD (0157H),HL   ; store in BIOSBASE
    0x21, 0x59, 0x01,   # LD HL,0159H     ; point at init string

    # 010C: INIT_LOP
    0x7E,               # LD A,(HL)
    0x23,               # INC HL
    0xB7,               # OR A
    0x28, 0x05,         # JR Z,+5          → 0116 MAIN_LOP
    0xCD, 0x4C, 0x01,   # CALL 014CH       AUXOUT_A
    0x18, 0xF6,         # JR -10           → 010C INIT_LOP

    # 0116: MAIN_LOP
    0xCD, 0x3C, 0x01,   # CALL 013CH       AUXIST_F
    0xB7,               # OR A
    0x28, 0x0B,         # JR Z,+11         → 0127 CHK_CON
    0xCD, 0x44, 0x01,   # CALL 0144H       READER_F
    0x5F,               # LD E,A
    0x0E, 0x02,         # LD C,2           BDOS fn 2 = console out
    0xCD, 0x05, 0x00,   # CALL 0005H       BDOS
    0x18, 0xEF,         # JR -17           → 0116 MAIN_LOP

    # 0127: CHK_CON
    0x0E, 0x0B,         # LD C,11          BDOS fn 11 = console status
    0xCD, 0x05, 0x00,   # CALL 0005H       BDOS
    0xB7,               # OR A
    0x28, 0xE7,         # JR Z,-25         → 0116 MAIN_LOP
    0x0E, 0x01,         # LD C,1           BDOS fn 1 = console input (echoed)
    0xCD, 0x05, 0x00,   # CALL 0005H       BDOS
    0xFE, 0x03,         # CP 03H           Ctrl-C?
    0xC8,               # RET Z            yes → exit to CP/M
    0xCD, 0x4C, 0x01,   # CALL 014CH       AUXOUT_A
    0x18, 0xDA,         # JR -38           → 0116 MAIN_LOP

    # 013C: AUXIST_F — B_AUXIST (BIOS+54)
    0x2A, 0x57, 0x01,   # LD HL,(BIOSBASE)
    0x11, 0x36, 0x00,   # LD DE,54
    0x19,               # ADD HL,DE
    0xE9,               # JP (HL)

    # 0144: READER_F — B_READER (BIOS+21)
    0x2A, 0x57, 0x01,   # LD HL,(BIOSBASE)
    0x11, 0x15, 0x00,   # LD DE,21
    0x19,               # ADD HL,DE
    0xE9,               # JP (HL)

    # 014C: AUXOUT_A — B_AUXOUT (BIOS+18), char in A on entry
    0xF5,               # PUSH AF
    0x2A, 0x57, 0x01,   # LD HL,(BIOSBASE)
    0x11, 0x12, 0x00,   # LD DE,18
    0x19,               # ADD HL,DE
    0xF1,               # POP AF
    0x4F,               # LD C,A           BIOS expects char in C
    0xE9,               # JP (HL)

    # 0157: BIOSBASE — filled at runtime
    0x00, 0x00,

    # 0159: ATZSTR = "ATE0\r\0"  (echo off so BDOS fn1 handles it)
    0x41, 0x54, 0x45, 0x30, 0x0D, 0x00,
])

assert len(CODE) == 95, f"Expected 95 bytes, got {len(CODE)}"

out = sys.argv[1] if len(sys.argv) > 1 else "MODEM.COM"
with open(out, "wb") as f:
    f.write(CODE)
print(f"Written {len(CODE)} bytes → {out}")
print("Copy to SD card as  A/0/MODEM.COM  (uppercase)")
print("Run in CP/M:  A0>MODEM")
print("Ctrl-C exits back to CP/M.")
