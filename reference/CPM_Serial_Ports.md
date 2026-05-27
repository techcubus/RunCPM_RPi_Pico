# CP/M Serial Port Implementation — Technical Reference

## Overview

CP/M provides serial/auxiliary device access through two independent mechanisms that coexist:

1. **The BIOS AUX device** — a standardised, hardware-independent path through the BIOS jump table
2. **Direct Z80 I/O port access** — hardware-specific `IN`/`OUT` instructions to UART chip registers

Popular communications programs (MODEM7, MEX, BYE) almost universally chose direct port access for performance and control, but used **overlay files** to make port addresses reconfigurable. This means both approaches work in practice; you just need to know which one a given program uses.

---

## 1. The Four Logical Devices

CP/M defines exactly four logical I/O devices:

| Name   | Direction    | Original purpose      | Modern equivalent       |
|--------|--------------|-----------------------|-------------------------|
| CON:   | Bidirectional| User terminal         | USB serial / console    |
| LST:   | Write-only   | Line printer          | Printer port            |
| PUN:   | Write-only   | Paper tape punch      | **Auxiliary serial out**|
| RDR:   | Read-only    | Paper tape reader     | **Auxiliary serial in** |

PUN: and RDR: are the serial modem path. By CP/M 3 they were officially renamed PUNCH→AUXOUT and READER→AUXIN to reflect this reality.

---

## 2. The IOBYTE — Device Routing

**Location:** Memory address `0x0003` (Zero Page, readable/writable directly)

The IOBYTE is a single 8-bit register that routes each logical device to one of four physical devices. Each device gets a 2-bit field:

```
  Bit 7   6  |  5   4  |  3   2  |  1   0
  └── LST ──┘  └─ PUN ─┘  └─ RDR ─┘  └─ CON ─┘
```

### Bit values per device

| Bits | CON:         | RDR:         | PUN:         | LST:         |
|------|--------------|--------------|--------------|--------------|
| 00   | TTY:         | TTY:         | TTY:         | TTY:         |
| 01   | CRT:         | PTR:         | PTP:         | CRT:         |
| 10   | BAT:         | UR1:         | UP1:         | LPT:         |
| 11   | UC1:         | UR2:         | UP2:         | UL1:         |

Physical device names are archaic and hardware-defined. The important values are:
- **TTY:** (00) — typically the main serial port
- **UC1:/UR1:/UR2:/UP1:/UP2:** — user-defined; implementation-specific physical ports

### IOBYTE default value

The standard CP/M reset value is `0x3D` (binary `00111101`):
- CON: → CRT: (01)
- RDR: → PTR: (11) — means "use UR2" on most systems
- PUN: → PTP: (11) — means "use UP2"
- LST: → CRT: (10)

RunCPM patches `0x3D` into address `0x0003` at each warm boot (`_PatchCPM()`).

### Accessing the IOBYTE

**From Z80 assembly (direct):**
```asm
LD A, (0003h)   ; read current IOBYTE
LD (0003h), A   ; write new IOBYTE
```

**Via BDOS (safe, works on CP/M 86 too):**
```asm
LD C, 7         ; BDOS function 7: Get IOBYTE
CALL 0005h
; IOBYTE now in A

LD C, 8         ; BDOS function 8: Set IOBYTE
LD E, <value>
CALL 0005h
```

Some modem programs redirect CON: to UC1: (bits 0-1 = 11) so that all console I/O flows through the serial port. This lets a dumb terminal drive the system remotely.

---

## 3. BDOS Serial Functions

BDOS calls go to address `0x0005`. Pass function number in register C, parameter in register E. Return value in register A (and L on some versions).

| Function | C= | Direction | Description |
|----------|----|-----------|-------------|
| A_READ   | 3  | Input     | Read one character from RDR: (auxiliary input). **May block forever** if device never provides data. Returns character in A. |
| A_WRITE  | 4  | Output    | Write character in E to PUN: (auxiliary output). **May block** if device not ready. |
| C_STAT   | 11 | Status    | Console input status. Returns 0 = no data, nonzero = data ready. No equivalent standard call for RDR: status in CP/M 2.2. |
| Get IOBYTE | 7 | —       | Return current IOBYTE in A. |
| Set IOBYTE | 8 | —       | Set IOBYTE to value in E. |

**The critical gap:** CP/M 2.2 has no BDOS call to check RDR: status without blocking. Programs that need non-blocking reads must either use direct BIOS calls or poll via direct port I/O. This is why most comms software bypasses BDOS entirely.

---

## 4. BIOS Jump Table — Serial Entry Points

The BIOS sits at a fixed address (in RunCPM: `BIOSjmppage = 0xFF00`). It exposes a jump table where each entry is 3 bytes (`JP address`). Entry points are numbered from 0 and spaced 3 bytes apart.

### Full jump table (CP/M 2.2 + CP/M 3 extensions)

| Entry# | Offset | Function   | CP/M name | Parameters          | Returns             |
|--------|--------|------------|-----------|---------------------|---------------------|
| 0      | +0     | BOOT       | B_BOOT    | —                   | —                   |
| 1      | +3     | WBOOT      | B_WBOOT   | —                   | —                   |
| 2      | +6     | CONST      | B_CONST   | —                   | A=FF (ready) or 00  |
| 3      | +9     | CONIN      | B_CONIN   | —                   | A=character         |
| 4      | +12    | CONOUT     | B_CONOUT  | C=character         | —                   |
| 5      | +15    | LIST       | B_LIST    | C=character         | —                   |
| **6**  | **+18**| **PUNCH**  | **B_AUXOUT** | **C=character**  | **—**               |
| **7**  | **+21**| **READER** | **B_READER** | **—**            | **A=character**     |
| 8      | +24    | HOME       | B_HOME    | —                   | —                   |
| 9      | +27    | SELDSK     | B_SELDSK  | C=drive#            | HL=DPH addr or 0    |
| 10     | +30    | SETTRK     | B_SETTRK  | BC=track#           | —                   |
| 11     | +33    | SETSEC     | B_SETSEC  | BC=sector#          | —                   |
| 12     | +36    | SETDMA     | B_SETDMA  | BC=DMA address      | —                   |
| 13     | +39    | READ       | B_READ    | —                   | A=0 (ok) or error   |
| 14     | +42    | WRITE      | B_WRITE   | C=deblock type      | A=0 (ok) or error   |
| 15     | +45    | LISTST     | B_LISTST  | —                   | A=FF or 00          |
| 16     | +48    | SECTRAN    | B_SECTRAN | BC=sector, DE=table | HL=physical sector  |
| 17     | +51    | CONOST     | B_CONOST  | —                   | A=FF or 00          |
| **18** | **+54**| **AUXIST** | **B_AUXIST** | **—**            | **A=FF (ready) or 00** |
| **19** | **+57**| **AUXOST** | **B_AUXOST** | **—**            | **A=FF (ready) or 00** |
| 20     | +60    | DEVTBL     | B_DEVTBL  | —                   | HL=device table     |
| 21     | +63    | DEVINI     | B_DEVINI  | C=device#           | —                   |
| 22     | +66    | DRVTBL     | B_DRVTBL  | —                   | HL=drive table      |
| 23     | +69    | MULTIO     | B_MULTIO  | C=sector count      | —                   |
| 24     | +72    | FLUSH      | B_FLUSH   | —                   | A=0 (ok) or error   |
| 25     | +75    | MOVE       | B_MOVE    | BC=count,DE=dst,HL=src | —              |
| 26     | +78    | TIME       | B_TIME    | C=0(get)/1(set)     | —                   |
| 27     | +81    | SELMEM     | B_SELMEM  | —                   | —                   |
| 28     | +84    | SETBNK     | B_SETBNK  | A=bank#             | —                   |
| 29     | +87    | XMOVE      | B_XMOVE   | C=dest bank, B=src  | —                   |
| 30     | +90    | USERF      | B_USERF   | —                   | — (CCP return)      |
| 31     | +93    | RESERV1    | —         | —                   | —                   |
| 32     | +96    | RESERV2    | —         | —                   | —                   |

**B_AUXIST and B_AUXOST are CP/M 3 additions.** CP/M 2.2 did not define them; programs targeting CP/M 2.2 only cannot rely on them. RunCPM implements them because it aims for CP/M 3 compatibility.

### Calling BIOS directly from Z80 assembly

Programs can call BIOS functions directly by jumping to the jump table:

```asm
BIOSBASE EQU 0FF00h     ; RunCPM BIOS base (varies by system)

PUNCH   EQU BIOSBASE+18 ; AUXOUT/PUNCH entry
READER  EQU BIOSBASE+21 ; READER/AUXIN entry
AUXIST  EQU BIOSBASE+54 ; AUX input status (CP/M 3)
AUXOST  EQU BIOSBASE+57 ; AUX output status (CP/M 3)

; Send character in A to auxiliary port:
LD C, A
CALL PUNCH

; Read character from auxiliary port (blocks):
CALL READER
; character now in A

; Check if AUX input ready (CP/M 3 only):
CALL AUXIST
; A=FFh if ready, A=00h if not
```

---

## 5. Direct Z80 Port I/O — How Modem Programs Work

Because CP/M 2.2 lacks a non-blocking auxiliary status call, serious communications software uses `IN`/`OUT` instructions directly against UART hardware registers. This is faster and provides full control over the UART.

### Z80 I/O instructions

```asm
IN  A, (port)       ; read 8-bit value from port into A
OUT (port), A       ; write A to port
```

Ports are 8-bit addresses (0x00–0xFF). The Z80 also supports 16-bit port addressing (`IN r,(C)` / `OUT (C),r`) where port comes from register C.

### Common UART chips and register layouts

#### Intel 8250 / 16550 (PC-style, common on RC2014 and clones)

The 8250 occupies 8 consecutive port addresses. Base address is hardware-specific.

| Offset | Read                           | Write                           |
|--------|--------------------------------|---------------------------------|
| +0     | RBR — Receiver Buffer Register | THR — Transmit Holding Register |
| +1     | IER — Interrupt Enable         | IER — Interrupt Enable          |
| +2     | IIR — Interrupt ID (read only) | FCR — FIFO Control (16550 only) |
| +3     | LCR — Line Control             | LCR — Line Control              |
| +4     | MCR — Modem Control            | MCR — Modem Control             |
| +5     | LSR — Line Status              | (factory test only)             |
| +6     | MSR — Modem Status             | (factory test only)             |
| +7     | SCR — Scratch register         | SCR — Scratch register          |

**Critical status bits (LSR at base+5):**

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | DR   | Data Ready — a received byte is waiting in RBR |
| 1   | OE   | Overrun Error |
| 2   | PE   | Parity Error |
| 3   | FE   | Framing Error |
| 4   | BI   | Break Interrupt |
| 5   | THRE | Transmit Holding Register Empty — safe to write next byte |
| 6   | TEMT | Transmitter Empty — both THR and shift register empty |
| 7   | —    | Error in FIFO (16550 FIFO mode only) |

**Critical status bits (MSR at base+6 — Modem Status):**

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | DCTS | Delta CTS — CTS changed since last read |
| 1   | DDSR | Delta DSR |
| 2   | TERI | Trailing Edge Ring Indicator |
| 3   | DDCD | Delta DCD — carrier changed since last read |
| 4   | CTS  | Clear To Send (current state) |
| 5   | DSR  | Data Set Ready (current state) |
| 6   | RI   | Ring Indicator (current state) |
| 7   | DCD  | Data Carrier Detect — **1 = modem connected** |

**Typical modem program polling loop:**

```asm
UBASE EQU 080h          ; base port address (hardware-specific)

; Wait until safe to transmit, then send A:
TXWAIT: IN  A, (UBASE+5)   ; read LSR
        AND 020h            ; test THRE (bit 5)
        JR  Z, TXWAIT       ; loop if not ready
        LD  A, <char>
        OUT (UBASE), A      ; write to THR

; Check if received byte is waiting (non-blocking):
RXTEST: IN  A, (UBASE+5)   ; read LSR
        AND 001h            ; test DR (bit 0)
        RET Z               ; return if nothing waiting
        IN  A, (UBASE)      ; read RBR — character in A

; Check DCD (is modem connected?):
CDTEST: IN  A, (UBASE+6)   ; read MSR
        AND 080h            ; test DCD (bit 7)
        RET                 ; Z set if no carrier
```

#### Zilog Z80 SIO (used on many original Z80 systems)

Two channels (A and B), typically one per serial port. Each channel has two port addresses: one for data, one for control/status.

```asm
SIOA_D EQU 00h     ; SIO channel A data
SIOA_C EQU 02h     ; SIO channel A control/status
SIOB_D EQU 01h     ; SIO channel B data
SIOB_C EQU 03h     ; SIO channel B control/status

; Check RX ready (read RR0, bit 0):
        XOR A
        OUT (SIOA_C), A    ; select Read Register 0
        IN  A, (SIOA_C)
        AND 001h           ; bit 0 = RX character available

; Check TX empty (RR0, bit 2):
        XOR A
        OUT (SIOA_C), A
        IN  A, (SIOA_C)
        AND 004h           ; bit 2 = TX buffer empty
```

#### Motorola MC2661B / 2661-2 (used in some Z-100 and other systems)

Similar concept to 8250 but different register layout. Typically 4 registers:
- Mode Register 1 / Mode Register 2 (accessed sequentially on first two writes)
- Command Register (write) / Status Register (read)
- Data Register (transmit write / receive read)

Status register bits vary; always consult the chip datasheet.

---

## 6. The Overlay Technique

MODEM7, MEX, BYE and similar programs ship as a `.COM` file with a **hardware overlay region** near the start. The overlay is a patch blob that is merged into the `.COM` to configure port addresses for a specific system.

### How it works

1. The `.COM` file reserves a block of bytes (typically at a fixed offset like `0100h + 200h`) filled with placeholder `NOP`s or zeros.
2. The user assembles (or selects) an overlay `.HEX`/`.COM` file for their hardware.
3. A patching tool (or the program's built-in install option) merges the overlay into the `.COM`.
4. The overlay provides the actual `IN`/`OUT` port addresses and any chip-specific initialization.

### Typical overlay structure for MODEM7

```asm
; Overlay template — fill in UBASE for your hardware
UBASE   EQU 080h        ; ← change this for your port base

MODINIT:                ; Modem initialization
        IN  A, (UBASE+3)    ; read LCR
        ; ... baud rate setup ...
        RET

MODSTAT:                ; Return modem status
        IN  A, (UBASE+6)    ; MSR
        AND 080h            ; DCD bit
        RET                 ; NZ = carrier present

MODRXR:                 ; RX ready check (NZ = byte waiting)
        IN  A, (UBASE+5)    ; LSR
        AND 001h
        RET

MODRX:                  ; Receive one character into A
        CALL MODRXR
        JR  Z, MODRX        ; wait
        IN  A, (UBASE)
        RET

MODTXR:                 ; TX ready check (NZ = can send)
        IN  A, (UBASE+5)
        AND 020h
        RET

MODTX:                  ; Transmit A
        PUSH AF
MODTX1: CALL MODTXR
        JR  Z, MODTX1
        POP AF
        OUT (UBASE), A
        RET
```

For RunCPM the overlay just needs to use whatever `UART_BASE` we define in `globals.h`. We control what ports the Z80 sees.

---

## 7. XMODEM File Transfer Protocol

XMODEM is the standard file transfer protocol for CP/M modem programs and runs entirely on top of the serial character stream described above.

### Frame format

```
SOH (01h) | BLOCK# (1 byte) | ~BLOCK# (1 byte) | DATA (128 bytes) | CHECKSUM (1 byte)
```

Block numbers start at 1 and increment. Checksum is the simple arithmetic sum of the 128 data bytes, modulo 256.

### Protocol flow

```
Receiver sends: NAK (15h) — signals ready, requests checksum mode
               or 'C' (43h) — signals ready, requests CRC mode

Sender sends: SOH block, waits for ACK (06h) or NAK (15h)
              ACK → send next block
              NAK → retransmit current block (up to 10 times)

End of file: Sender sends EOT (04h), receiver responds ACK
```

XMODEM-CRC replaces the single checksum byte with a 2-byte CRC-16/CCITT. Most CP/M implementations support both.

---

## 8. How RunCPM Implements This

RunCPM intercepts Z80 I/O at two points in `abstraction_arduino.h`:

```c
void   _HardwareOut(uint32 Port, uint32 Value) {}  // Z80 OUT instruction
uint32 _HardwareIn (uint32 Port)               {}  // Z80 IN instruction
```

And BIOS AUX calls in `cpm.h`:

```c
case B_AUXOUT: { /* C register holds character */ break; }
case B_READER: { SET_HIGH_REGISTER(AF, 0x1a); break; }  // 0x1a = ^Z = EOF
case B_AUXIST: { SET_HIGH_REGISTER(AF, 0x00); break; }  // always not ready
case B_AUXOST: { SET_HIGH_REGISTER(AF, 0x00); break; }  // always not ready
```

With `INT_HANDOFF` defined (which it is in this codebase), **all** Z80 `IN`/`OUT` instructions reach `_HardwareIn`/`_HardwareOut` — none are reserved for BIOS/BDOS triggering. This means all 256 port addresses (0x00–0xFF) are freely available for our virtual UART.

---

## Sources

- [CP/M IOBYTE — seasip.info](https://www.seasip.info/Cpm/iobyte.html)
- [CP/M BIOS functions — seasip.info](https://www.seasip.info/Cpm/bios.html)
- [CP/M BDOS system calls — seasip.info](https://www.seasip.info/Cpm/bdos.html)
- [CP/M serial device mapping — Kevin Boone](https://kevinboone.me/cpmserial.html)
- [Serial file transfer under CP/M-80 — comp.os.cpm](https://groups.google.com/g/comp.os.cpm/c/d1Y67fl5Shc)
