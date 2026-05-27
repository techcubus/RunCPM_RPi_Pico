#ifndef MODEM_H
#define MODEM_H

// =========================================================================================
// modem.h — WiFi AT modem engine for RunCPM on Pico 2 W
//
// Provides a Hayes-compatible AT command interface over WiFi TCP for CP/M programs.
// Two access paths from the Z80 side:
//
//   1. BIOS AUX device  (B_AUXOUT / B_READER / B_AUXIST / B_AUXOST in cpm.h)
//      — portable path, works with programs using BDOS functions 3 & 4
//
//   2. Virtual UART I/O ports  (_HardwareIn / _HardwareOut in abstraction_arduino.h)
//      — direct port path, works with MODEM7/MEX overlays
//      — base address defined by UART_BASE in globals.h
//
// State machine:
//   MODEM_OFFLINE   → WiFi not connected
//   MODEM_COMMAND   → AT command mode (may have active TCP connection via ATO)
//   MODEM_ONLINE    → data mode, passing bytes between Z80 and TCP socket
//   MODEM_RINGING   → incoming TCP connection waiting for ATA
//
// TODO: implement body of each function below.
// =========================================================================================

#include <WiFi.h>

// -----------------------------------------------------------------------------------------
// Modem state
// -----------------------------------------------------------------------------------------
typedef enum {
    MODEM_OFFLINE  = 0,
    MODEM_COMMAND  = 1,
    MODEM_ONLINE   = 2,
    MODEM_RINGING  = 3
} ModemState;

static ModemState modem_state = MODEM_OFFLINE;

// Active TCP client (outgoing or accepted incoming)
static WiFiClient modem_client;

// Incoming-connection server (listen port = S14 register, default 23)
static WiFiServer modem_server(23);

// -----------------------------------------------------------------------------------------
// S-registers
// -----------------------------------------------------------------------------------------
static uint8 s_reg[64] = {0};
// S0  = auto-answer ring count (0 = disabled)
// S1  = ring counter
// S2  = escape character (default '+' = 43)
// S12 = guard time in 20 ms units (default 50 = 1 second)
// S14 = listen port (default 23)
// S15 = telnet protocol enable (0 = raw TCP)

// -----------------------------------------------------------------------------------------
// Settings
// -----------------------------------------------------------------------------------------
static bool modem_echo    = true;   // ATE1
static bool modem_quiet   = false;  // ATQ0
static bool modem_verbose = true;   // ATV1

// -----------------------------------------------------------------------------------------
// RX ring buffer (TCP → Z80)
// -----------------------------------------------------------------------------------------
#define MODEM_RX_BUFSIZE 256
static uint8  rx_buf[MODEM_RX_BUFSIZE];
static uint16 rx_head = 0;
static uint16 rx_tail = 0;

static bool rx_buf_empty()               { return rx_head == rx_tail; }
static bool rx_buf_full()                { return ((rx_tail + 1) % MODEM_RX_BUFSIZE) == rx_head; }
static void rx_buf_push(uint8 ch)        { if (!rx_buf_full()) { rx_buf[rx_tail] = ch; rx_tail = (rx_tail + 1) % MODEM_RX_BUFSIZE; } }
static uint8 rx_buf_pop()               { uint8 ch = rx_buf[rx_head]; rx_head = (rx_head + 1) % MODEM_RX_BUFSIZE; return ch; }

// -----------------------------------------------------------------------------------------
// Public interface — called from cpm.h (BIOS) and abstraction_arduino.h (port I/O)
// -----------------------------------------------------------------------------------------

// Is there a byte waiting for the Z80 to read?
bool modem_rx_available() {
    return !rx_buf_empty();
}

// Read one byte (call only after modem_rx_available() returns true)
uint8 modem_read() {
    if (rx_buf_empty()) return 0x1a; // ^Z = EOF placeholder
    return rx_buf_pop();
}

// Is the modem ready to accept a byte from the Z80?
bool modem_tx_ready() {
    if (modem_state == MODEM_ONLINE) return modem_client.connected();
    return true; // command mode always accepts
}

// Z80 sends one byte to the modem
void modem_write(uint8 ch) {
    // TODO: implement — in data mode forward to TCP; in command mode add to AT command buffer
    (void)ch;
}

// Line Status Register byte (8250 base+5):
//   bit 0 = DR   (data ready — RX byte waiting)
//   bit 5 = THRE (transmit holding register empty — safe to send)
//   bit 6 = TEMT (transmitter empty)
uint8 modem_lsr() {
    uint8 lsr = 0;
    if (modem_rx_available())  lsr |= 0x01; // DR
    if (modem_tx_ready())      lsr |= 0x60; // THRE + TEMT
    return lsr;
}

// Modem Status Register byte (8250 base+6):
//   bit 7 = DCD (Data Carrier Detect — 1 when TCP connection is active)
uint8 modem_msr() {
    uint8 msr = 0;
    if (modem_state == MODEM_ONLINE && modem_client.connected()) msr |= 0x80; // DCD
    return msr;
}

// -----------------------------------------------------------------------------------------
// Send a result code string to the Z80 console (via _putcon)
// -----------------------------------------------------------------------------------------
static void modem_response(const char *msg) {
    // TODO: implement — respect modem_quiet and modem_verbose (numeric vs text codes)
    (void)msg;
}

// -----------------------------------------------------------------------------------------
// modem_init() — call from setup() after SD card and Serial are ready
// -----------------------------------------------------------------------------------------
void modem_init() {
    // Initialise S-registers to defaults
    memset(s_reg, 0, sizeof(s_reg));
    s_reg[2]  = '+';  // escape character
    s_reg[3]  = 13;   // CR
    s_reg[4]  = 10;   // LF
    s_reg[5]  = 8;    // BS
    s_reg[12] = 50;   // guard time (50 × 20ms = 1 second)
    s_reg[14] = 23;   // listen port

    // TODO: read MODEM.CFG from SD card for WiFi credentials
    // TODO: WiFi.begin(ssid, pass) and wait for connection
    // TODO: modem_server.begin() on s_reg[14]

    modem_state = MODEM_COMMAND;

    _puts("Modem            at [\e[1mCOMMAND\e[0m]\r\n");
}

// -----------------------------------------------------------------------------------------
// modem_update() — call on every BIOS entry to keep the state machine alive
// -----------------------------------------------------------------------------------------
void modem_update() {
    // TODO: implement:
    //   1. Drain incoming TCP data into rx_buf
    //   2. Detect TCP disconnect → send NO CARRIER, return to COMMAND
    //   3. In COMMAND mode, check modem_server.accept() for incoming connections
    //   4. Handle +++ guard-time escape detection
    //   5. Handle S0 auto-answer
}

#endif // MODEM_H
