; MODEM.COM — minimal CP/M modem bridge
; Bridges CP/M console (ttyACM0) ↔ RunCPM virtual WiFi modem via BIOS AUX calls.
;
; BIOS entry points used:
;   B_AUXOUT  BIOS+18  send byte to modem   (char in C register)
;   B_READER  BIOS+21  receive byte          (returns char in A)
;   B_AUXIST  BIOS+54  AUX input status      (returns 0xFF=ready, 0x00=empty)
;
; On startup sends "ATE0\r" to disable modem echo (BDOS fn 1 echoes instead).
; Ctrl-C at the CP/M console exits.
;
; Memory map (ORG 100H):
;   0100  START      get BIOS base, send init string
;   010C  INIT_LOP   drip-feed init string to modem
;   0116  MAIN_LOP   poll modem → console, poll console → modem
;   0127  CHK_CON    console side of main loop
;   013C  AUXIST_F   BIOS B_AUXIST stub (BIOS+54)
;   0144  READER_F   BIOS B_READER stub (BIOS+21)
;   014C  AUXOUT_A   BIOS B_AUXOUT stub (BIOS+18, char in A on entry)
;   0157  BIOSBASE   2-byte BIOS base address (filled at startup)
;   0159  ATZSTR     "ATE0\r" + NUL
;
        ORG     0100H
BDOS    EQU     0005H

; ---- startup: locate BIOS, send init string ----
START:
        LD      HL,(0001H)      ; warm-boot vector = BIOS+3
        DEC     HL
        DEC     HL
        DEC     HL              ; HL = BIOS base
        LD      (BIOSBASE),HL
        LD      HL,ATZSTR       ; point at init string

INIT_LOP:
        LD      A,(HL)
        INC     HL
        OR      A
        JR      Z,MAIN_LOP      ; NUL terminator → done
        CALL    AUXOUT_A
        JR      INIT_LOP

; ---- main loop ----
MAIN_LOP:
        CALL    AUXIST_F        ; A = FFH if modem byte waiting
        OR      A
        JR      Z,CHK_CON
        CALL    READER_F        ; A = byte from modem
        LD      E,A
        LD      C,2             ; BDOS fn 2 — console output
        CALL    BDOS
        JR      MAIN_LOP

CHK_CON:
        LD      C,11            ; BDOS fn 11 — console status
        CALL    BDOS
        OR      A
        JR      Z,MAIN_LOP
        LD      C,1             ; BDOS fn 1 — console input (echoes char)
        CALL    BDOS
        CP      03H             ; Ctrl-C ?
        RET     Z               ; yes → back to CP/M
        CALL    AUXOUT_A        ; no  → forward to modem
        JR      MAIN_LOP

; ---- BIOS call stubs ----

; B_AUXIST — AUX input status (BIOS+54)
; Returns A = FFH (ready) or 00H (empty)
AUXIST_F:
        LD      HL,(BIOSBASE)
        LD      DE,54
        ADD     HL,DE
        JP      (HL)

; B_READER — AUX input char (BIOS+21)
; Returns char in A
READER_F:
        LD      HL,(BIOSBASE)
        LD      DE,21
        ADD     HL,DE
        JP      (HL)

; B_AUXOUT — AUX output (BIOS+18)
; Entry: A = char to send (BIOS reads char from C register)
AUXOUT_A:
        PUSH    AF
        LD      HL,(BIOSBASE)
        LD      DE,18
        ADD     HL,DE
        POP     AF
        LD      C,A
        JP      (HL)

; ---- data ----
BIOSBASE: DW    0000H           ; filled at startup
ATZSTR:   DB    'ATE0',0DH,00H  ; echo off, then CR

        END     START
