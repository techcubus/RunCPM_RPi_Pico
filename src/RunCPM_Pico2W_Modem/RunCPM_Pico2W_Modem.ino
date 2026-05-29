/*
  RunCPM for Raspberry Pi Pico 2 W — WiFi AT Modem variant
  Based on RunCPM v6.9 by Marcelo Dantas
  Pico 2 W port and modem additions by Guido Lehwalder / techcubus

  SD card wiring (SPI0):
    MISO  GPIO 16  Pin 21
    CS    GPIO 17  Pin 22
    SCK   GPIO 18  Pin 24
    MOSI  GPIO 19  Pin 25

  Board:   Raspberry Pi Pico 2 W  (RP2350 + CYW43 WiFi)
  Package: earlephilhower/arduino-pico
  Library: SdFat by Greiman (Library Manager)
*/

#include "globals.h"

// =========================================================================================
// Revision
// =========================================================================================
#define GL_REV "GL20251130.0-Pico2W-Modem"

#include <SPI.h>
#include <WiFi.h>

#define SPI_DRIVER_SELECT    0
#define SDFAT_FILE_TYPE      1   // required for RPi Pico
#define ENABLE_DEDICATED_SPI 1

#include <SdFat.h>

// =========================================================================================
// Board — Raspberry Pi Pico 2 W
// LED is on the CYW43 chip; arduino-pico exposes it via LED_BUILTIN automatically.
// =========================================================================================
#include "hardware/pico/pico2w_sd_spi.h"

// =========================================================================================
// Timing constants for LED blink
// =========================================================================================
#define sDELAY 200
#define DELAY  400

#include "abstraction_arduino.h"

// =========================================================================================
// Serial port speed
// =========================================================================================
#define SERIALSPD 115200

// =========================================================================================
// PUN: / LST: virtual device files
// =========================================================================================
#ifdef USE_PUN
File32 pun_dev;
int pun_open = FALSE;
#endif
#ifdef USE_LST
File32 lst_dev;
int lst_open = FALSE;
#endif

#include "ram.h"
#include "console.h"
#include CPU
#include "disk.h"
#include "host.h"
#include "modem.h"   // WiFi AT modem engine — must come before cpm.h
#include "cpm.h"
#ifdef CCP_INTERNAL
#include "ccp.h"
#endif

// =========================================================================================
void setup(void) {
// =========================================================================================

    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    Serial.begin(SERIALSPD);
    while (!Serial) {
        digitalWrite(LED, HIGH ^ LEDinv);
        delay(sDELAY);
        digitalWrite(LED, LOW ^ LEDinv);
        delay(DELAY);
    }

#ifdef DEBUGLOG
    _sys_deletefile((uint8 *)LogName);
#endif

    _clrscr();

    _puts("CP/M Emulator \e[1mv" VERSION "\e[0m   by   \e[1mMarcelo  Dantas\e[0m\r\n");

#ifdef ABDOS
    _puts("     (A)BDOS.SYS     by   \e[1mPavel    Zampach\e[0m\r\n");
#endif

    _puts("-------------------------------------------------\r\n");
#if defined(ARDUINO_RASPBERRY_PI_PICO2W)
    _puts("     running    on   Raspberry Pi  [\e[1mPico 2 W\e[0m]\r\n");
    _puts("     compiled with   RP2350        [\e[1mv5.4.3\e[0m] \r\n");
#else
    _puts("     running    on   Raspberry Pi  [\e[1mPico W\e[0m]\r\n");
    _puts("     compiled with   RP2040        [\e[1mv5.4.3\e[0m] \r\n");
#endif
    _puts("               and   SDFat         [\e[1mv2.3.1\e[0m] \r\n");
    _puts("                     Revision      [\e[1m");
    _puts(GL_REV);
    _puts("\e[0m]\r\n");
    _puts("-------------------------------------------------\r\n");

    _puts("BIOS              at [\e[1m0x");
    _puthex16(BIOSjmppage);
    _puts("\e[0m]\r\n");

#ifdef ABDOS
    _puts("ABDOS.SYS \e[1menabled\e[0m at [\e[1m0x");
    _puthex16(BDOSjmppage);
    _puts("\e[0m]\r\n");
#else
    _puts("BDOS              at [\e[1m0x");
    _puthex16(BDOSjmppage);
    _puts("\e[0m]\r\n");
#endif

#ifdef INT_HANDOFF
    _puts("BIOS/BDOS     method [\e[1minterrupt    handoff      \e[0m]\r\n");
#else
    _puts("BIOS/BDOS     method [\e[1mlegacy       IN/OUT call  \e[0m]\r\n");
#endif

    _puts("CCP               at [\e[1m0x");
    _puthex16(CCPaddr);
    _puts("\e[0m]     [\e[1m");
    _puts(CCPname);
    _puts("\e[0m]\r\n");

#if BANKS > 1
    _puts("Banked Memory        [\e[1m");
    _puthex8(BANKS);
    _puts("\e[0m]banks\r\n");
#else
    _puts("Banked Memory        [\e[1m");
    _puthex8(BANKS);
    _puts("\e[0m]bank\r\n");
#endif

    _puts("Z80 CPU  Type        [\e[1m");
    _puts(CPU_IS);
    _puts("\e[0m]\r\n");

    Z80estimateClock();
#if defined(ARDUINO_RASPBERRY_PI_PICO2W)
    _puts("CPU-Clock            [\e[1m300Mhz\e[0m]\r\n");
#else
    _puts("CPU-Clock            [\e[1m260Mhz\e[0m]\r\n");
#endif

    _puts("Virtual UART         [\e[1m0x");
    _puthex8(UART_BASE);
    _puts("\e[0m]\r\n");

    // =================================================================================
    // SPI pin assignment
    // =================================================================================
    SPI.setRX(16);
    SPI.setCS(17);
    SPI.setSCK(18);
    SPI.setTX(19);

    // =================================================================================
    // SD card init
    // =================================================================================
#define SDMHZ_TXT "19"
#define SDMHZ     19
#define SS        17
#define SD_CONFIG SdSpiConfig(SS, DEDICATED_SPI, SD_SCK_MHZ(SDMHZ), &SPI)

    _puts("Init MicroSD-Card    [ \e[1m");
    if (SD.begin(SD_CONFIG)) {
        _puts(SDMHZ_TXT);
        _puts("Mhz\e[0m]\r\n");
        _puts("-------------------------------------------------\r\n");

        // =============================================================================
        // WiFi + modem init (reads MODEM.CFG from SD card)
        // =============================================================================
        modem_init();

        _puts("-------------------------------------------------\r\n");

        if (VersionCCP >= 0x10 || SD.exists(CCPname)) {
#ifdef ABDOS
            _PatchBIOS();
#endif
            while (true) {
                _puts(CCPHEAD);
                _PatchCPM();
                Status = STATUS_RUNNING;

#ifdef CCP_INTERNAL
                _ccp();
#else
                if (!_RamLoad((uint8 *)CCPname, CCPaddr, 0)) {
                    _puts("Unable to load the CCP.\r\nCPU halted.\r\n");
                    break;
                }
                if (firstBoot) {
                    if (_sys_exists((uint8 *)AUTOEXEC)) {
                        uint16 cmd = CCPaddr + 8;
                        uint8 bytesread = (uint8)_RamLoad((uint8 *)AUTOEXEC, cmd, 125);
                        uint8 blen = 0;
                        while (blen < bytesread && _RamRead(cmd + blen) > 31) blen++;
                        _RamWrite(cmd + blen, 0x00);
                        _RamWrite(--cmd, blen);
                    }
                    if (BOOTONLY) firstBoot = FALSE;
                }
                Z80reset();
                SET_LOW_REGISTER(BC, _RamRead(DSKByte));
                PC = CCPaddr;
                Z80run(cpuDelayInstructions);
#endif
                if (Status == STATUS_EXIT)
#ifdef DEBUG
#ifdef DEBUGONHALT
                {
                    Debug = 1;
                    Z80debug();
                }
#endif
#endif
                    break;

#ifdef USE_PUN
                if (pun_dev) _sys_fflush(pun_dev);
#endif
#ifdef USE_LST
                if (lst_dev) _sys_fflush(lst_dev);
#endif
            }
        } else {
            _puts("Unable to load CP/M CCP.\r\nCPU halted.\r\n");
        }
    } else {
        _puts("Unable to initialize SD card.\r\nCPU halted.\r\n");
    }
}

// =========================================================================================
// loop() — only reached if CP/M exits; blink LED as halted indicator
// =========================================================================================
void loop(void) {
    digitalWrite(LED, HIGH ^ LEDinv);
    delay(DELAY);
    digitalWrite(LED, LOW ^ LEDinv);
    delay(DELAY);
    digitalWrite(LED, HIGH ^ LEDinv);
    delay(DELAY);
    digitalWrite(LED, LOW ^ LEDinv);
    delay(DELAY * 4);
}
