#ifndef MODEM_H
#define MODEM_H

// =========================================================================================
// modem.h — WiFi AT modem engine for RunCPM on Pico 2 W
//
// Provides a Hayes-compatible AT command interface over WiFi TCP for CP/M programs.
// Two access paths from the Z80 side, both routing through the same engine:
//
//   1. BIOS AUX device  (B_AUXOUT / B_READER / B_AUXIST / B_AUXOST in cpm.h)
//      Portable path — works with programs using BDOS functions 3 & 4.
//
//   2. Virtual UART I/O ports  (_HardwareOut / _HardwareIn in abstraction_arduino.h)
//      Direct port path — works with MODEM7/MEX/BYE using overlays.
//      Base address: UART_BASE (default 0x80, defined in globals.h)
//        UART_BASE+0  RX (read) / TX (write)
//        UART_BASE+5  LSR: bit0=DR, bit5=THRE, bit6=TEMT
//        UART_BASE+6  MSR: bit7=DCD (1 when TCP connected)
//
// State machine:
//   MODEM_OFFLINE  — WiFi not connected
//   MODEM_COMMAND  — AT command mode
//   MODEM_ONLINE   — data mode, transparent TCP byte forwarding
//   MODEM_RINGING  — incoming TCP connection waiting for ATA or auto-answer
//
// Call order in main sketch:
//   modem_init()   — from setup(), after SD card is ready
//   modem_update() — from _Bios() entry, keeps state machine alive between Z80 calls
// =========================================================================================

// WiFi.h is included in the .ino before this header.
// globals.h types (uint8, uint16, uint32 ...) and UART_BASE are available.
// console.h (_putcon, _puts) is available.

// -----------------------------------------------------------------------------------------
// Modem state
// -----------------------------------------------------------------------------------------
typedef enum {
    MODEM_OFFLINE = 0,
    MODEM_COMMAND = 1,
    MODEM_ONLINE  = 2,
    MODEM_RINGING = 3
} ModemState;

static ModemState modem_state = MODEM_OFFLINE;

// Active TCP connection (outgoing or accepted incoming)
static WiFiClient modem_client;

// Incoming connection server
static WiFiServer *modem_server   = NULL;
static uint16     modem_listen_port = 0;

// Pending incoming client (held in RINGING state until ATA or auto-answer)
static WiFiClient modem_pending;

// -----------------------------------------------------------------------------------------
// S-registers
// -----------------------------------------------------------------------------------------
#define SREG_COUNT    64

static uint8 s_reg[SREG_COUNT];

#define S_AUTOANSWER   0   // rings before auto-answer (0 = off)
#define S_RINGCOUNT    1   // ring counter (read-only in practice)
#define S_ESCAPE       2   // escape character ASCII code (default '+' = 43)
#define S_CR           3   // carriage return ASCII code (default 13)
#define S_LF           4   // line feed ASCII code (default 10)
#define S_BS           5   // backspace ASCII code (default 8)
#define S_GUARDTIME   12   // +++ guard time in 20 ms units (default 50 = 1 s)
#define S_LISTENPORT  14   // TCP port to listen on (0 = disabled, default 23)
#define S_TELNET      15   // telnet protocol: 0=raw TCP (default), 1=telnet IAC

// -----------------------------------------------------------------------------------------
// Settings (AT command controlled)
// -----------------------------------------------------------------------------------------
static bool modem_echo    = true;   // ATE1 — echo command characters
static bool modem_quiet   = false;  // ATQ0 — send result codes
static bool modem_verbose = true;   // ATV1 — verbose result codes (vs numeric)

// -----------------------------------------------------------------------------------------
// WiFi credentials (loaded from MODEM.CFG on SD card)
// -----------------------------------------------------------------------------------------
static char modem_ssid[64];
static char modem_pass[64];

// -----------------------------------------------------------------------------------------
// AT command input buffer
// -----------------------------------------------------------------------------------------
#define CMD_BUF_SIZE 81

static char  cmd_buf[CMD_BUF_SIZE];   // current command being typed
static uint8 cmd_len = 0;
static char  last_cmd[CMD_BUF_SIZE];  // saved for A/ repeat

// -----------------------------------------------------------------------------------------
// RX ring buffer — holds bytes received from TCP waiting for Z80 to read
// -----------------------------------------------------------------------------------------
#define MODEM_RX_BUFSIZE 256

static uint8  rx_buf[MODEM_RX_BUFSIZE];
static uint16 rx_head = 0;
static uint16 rx_tail = 0;

static inline bool  rx_empty()         { return rx_head == rx_tail; }
static inline bool  rx_full()          { return ((rx_tail + 1) % MODEM_RX_BUFSIZE) == rx_head; }
static inline void  rx_push(uint8 ch)  { if (!rx_full())  { rx_buf[rx_tail] = ch; rx_tail = (rx_tail + 1) % MODEM_RX_BUFSIZE; } }
static inline uint8 rx_pop()           { uint8 ch = rx_buf[rx_head]; rx_head = (rx_head + 1) % MODEM_RX_BUFSIZE; return ch; }

// -----------------------------------------------------------------------------------------
// +++ escape sequence state
// -----------------------------------------------------------------------------------------
static uint8  escape_count      = 0;
static uint32 escape_last_ms    = 0;
static uint32 online_last_tx_ms = 0;  // timestamp of last byte sent by Z80 in data mode

// -----------------------------------------------------------------------------------------
// Result code strings
// -----------------------------------------------------------------------------------------
#define RC_OK          "OK"
#define RC_CONNECT     "CONNECT"
#define RC_RING        "RING"
#define RC_NO_CARRIER  "NO CARRIER"
#define RC_ERROR       "ERROR"
#define RC_NO_DIALTONE "NO DIALTONE"
#define RC_BUSY        "BUSY"
#define RC_NO_ANSWER   "NO ANSWER"

// -----------------------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------------------
static void modem_response(const char *msg);
static void modem_hangup(bool send_no_carrier);
static void modem_enter_command_mode();
static void modem_process_command();
static void modem_dial(const char *dest);

// =========================================================================================
// Internal helpers
// =========================================================================================

// Push a string into the Z80-readable AUX RX buffer so CP/M programs receive it
// via B_READER / port 0x80.
// NOTE: Do NOT also call _putch() here — CP/M programs (MODEM.COM etc.) read these
// bytes from rx_buf and output them via BDOS fn 2 themselves.  Calling _putch()
// here as well causes double output and garbled terminal display.
static void modem_to_rx(const char *s) {
    while (*s) {
        if (!rx_full()) rx_push((uint8)*s);
        s++;
    }
}

// Send a result code to the Z80 (AUX RX buffer), respecting ATV / ATQ
static void modem_response(const char *msg) {
    if (modem_quiet) return;
    if (modem_verbose) {
        modem_to_rx("\r\n"); modem_to_rx(msg); modem_to_rx("\r\n");
    } else {
        // Numeric equivalents
        if      (strcmp(msg, RC_OK)         == 0) modem_to_rx("0\r");
        else if (strcmp(msg, RC_CONNECT)    == 0) modem_to_rx("1\r");
        else if (strcmp(msg, RC_RING)       == 0) modem_to_rx("2\r");
        else if (strcmp(msg, RC_NO_CARRIER) == 0) modem_to_rx("3\r");
        else if (strcmp(msg, RC_ERROR)      == 0) modem_to_rx("4\r");
        else if (strcmp(msg, RC_NO_ANSWER)  == 0) modem_to_rx("8\r");
        else { modem_to_rx(msg); modem_to_rx("\r"); }
    }
}

// Close TCP connection and return to command mode
static void modem_hangup(bool send_no_carrier) {
    if (modem_client.connected()) modem_client.stop();
    modem_state   = MODEM_COMMAND;
    escape_count  = 0;
    if (send_no_carrier) modem_response(RC_NO_CARRIER);
}

// Escape from data mode to command mode (TCP stays open)
static void modem_enter_command_mode() {
    modem_state  = MODEM_COMMAND;
    escape_count = 0;
    modem_response(RC_OK);
}

// Open outgoing TCP connection — ATDT host:port
static void modem_dial(const char *dest) {
    if (WiFi.status() != WL_CONNECTED) {
        modem_response(RC_NO_DIALTONE);
        return;
    }
    if (modem_client.connected()) {
        modem_response(RC_BUSY);
        return;
    }

    // Parse "host:port" — port defaults to 23
    char host[128];
    strncpy(host, dest, 127);
    host[127] = 0;

    uint16 port = 23;
    char *colon = strrchr(host, ':');
    if (colon) {
        *colon = 0;
        port = (uint16)atoi(colon + 1);
        if (port == 0) port = 23;
    }

    if (host[0] == 0) { modem_response(RC_ERROR); return; }

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", port);
    _puts("\r\nDialing          [\e[1m");
    _puts(host); _puts(":"); _puts(portstr);
    _puts("\e[0m] ");

    modem_client.setNoDelay(true);
    if (modem_client.connect(host, port)) {
        _puts("\e[1mOK\e[0m\r\n");
        modem_state        = MODEM_ONLINE;
        online_last_tx_ms  = millis();
        escape_count       = 0;
        modem_response(RC_CONNECT);
    } else {
        _puts("\e[1mFAILED\e[0m\r\n");
        modem_response(RC_NO_ANSWER);
    }
}

// Parse and execute one AT command line (cmd_buf must be NUL-terminated, uppercase)
static void modem_process_command() {
    strncpy(last_cmd, cmd_buf, CMD_BUF_SIZE - 1);

    char *p = cmd_buf;

    // Must start with AT (already uppercased in modem_write)
    if (p[0] != 'A' || p[1] != 'T') { modem_response(RC_ERROR); return; }
    p += 2;

    // Bare AT → OK
    if (*p == 0) { modem_response(RC_OK); return; }

    bool ok = true;

    while (*p && ok) {
        char c = *p++;

        switch (c) {

        // Z / &F — reset to defaults
        case 'Z':
            modem_echo = true; modem_quiet = false; modem_verbose = true;
            s_reg[S_AUTOANSWER] = 0;
            s_reg[S_GUARDTIME]  = 50;
            modem_response(RC_OK);
            return;

        // & prefix
        case '&': {
            char sub = *p++;
            switch (sub) {
            case 'C':   // &C — DCD mode (no-op: we control DCD via MSR bit 7)
                while (*p >= '0' && *p <= '9') p++;
                break;
            case 'D':   // &D — DTR mode (no-op)
                while (*p >= '0' && *p <= '9') p++;
                break;
            case 'F':
                modem_echo = true; modem_quiet = false; modem_verbose = true;
                break;
            case 'K':   // &K — flow control (no-op)
                while (*p >= '0' && *p <= '9') p++;
                break;
            case 'W':
                // TODO: write settings back to MODEM.CFG on SD card
                break;
            default:
                ok = false;
            }
            break;
        }

        // B — baud negotiation mode (no-op: we're TCP)
        case 'B':
            while (*p >= '0' && *p <= '9') p++;
            break;

        // E — echo
        case 'E':
            modem_echo = (*p != '0');
            if (*p == '0' || *p == '1') p++;
            break;

        // L — speaker volume (no-op: no speaker)
        case 'L':
            while (*p >= '0' && *p <= '9') p++;
            break;

        // M — speaker mode (no-op: no speaker)
        case 'M':
            while (*p >= '0' && *p <= '9') p++;
            break;

        // Q — quiet mode
        case 'Q':
            modem_quiet = (*p == '1');
            if (*p == '0' || *p == '1') p++;
            break;

        // V — verbose/numeric result codes
        case 'V':
            modem_verbose = (*p != '0');
            if (*p == '0' || *p == '1') p++;
            break;

        // X — result code level (no-op: we always send full word codes)
        case 'X':
            while (*p >= '0' && *p <= '9') p++;
            break;

        // Y — long-space disconnect (no-op)
        case 'Y':
            while (*p >= '0' && *p <= '9') p++;
            break;

        // I — identify
        case 'I':
            if (*p == '1') {
                p++;
                modem_to_rx("\r\nRunCPM v" VERSION " / " GL_REV "\r\n");
            } else {
                if (*p == '0') p++;
                modem_to_rx("\r\nRunCPM WiFi Modem\r\n");
            }
            break;

        // H — hang up
        case 'H':
            if (*p == '0' || *p == 0) {
                if (*p) p++;
                modem_hangup(modem_client.connected());
            }
            modem_response(RC_OK);
            return;

        // O — return to online data mode
        case 'O':
            if (*p == '0' || *p == 0) {
                if (*p) p++;
                if (modem_client.connected()) {
                    modem_state = MODEM_ONLINE;
                    modem_response(RC_CONNECT);
                } else {
                    modem_response(RC_NO_CARRIER);
                }
                return;
            }
            break;

        // A — answer incoming call
        case 'A':
            if (modem_state == MODEM_RINGING && modem_pending.connected()) {
                modem_client = modem_pending;
                modem_client.setNoDelay(true);
                modem_state       = MODEM_ONLINE;
                online_last_tx_ms = millis();
                escape_count      = 0;
                modem_response(RC_CONNECT);
            } else {
                modem_response(RC_NO_CARRIER);
            }
            return;

        // D — dial (DT or DP, T/P prefix optional)
        case 'D':
            if (*p == 'T' || *p == 'P') p++;  // skip tone/pulse indicator
            while (*p == ' ') p++;             // skip leading spaces
            modem_dial(p);
            return;  // dial consumes rest of line

        // S — S-register access: Sn=val or Sn?
        case 'S': {
            uint8 reg = 0;
            while (*p >= '0' && *p <= '9') reg = reg * 10 + (uint8)(*p++ - '0');
            if (reg >= SREG_COUNT) { ok = false; break; }

            if (*p == '=') {
                p++;
                uint16 val = 0;
                while (*p >= '0' && *p <= '9') val = val * 10 + (uint16)(*p++ - '0');
                s_reg[reg] = (uint8)(val > 255 ? 255 : val);
            } else if (*p == '?') {
                p++;
                char buf[8];
                snprintf(buf, sizeof(buf), "\r\n%u\r\n", s_reg[reg]);
                modem_to_rx(buf);
            } else {
                ok = false;
            }
            break;
        }

        // + prefix — WiFi control (AT+W...)
        case '+': {
            if (*p != 'W') { ok = false; break; }
            p++;
            char sub = *p++;

            switch (sub) {
            case '?': {  // AT+W? — WiFi status
                switch (WiFi.status()) {
                case WL_IDLE_STATUS:     modem_to_rx("\r\nWIFI IDLE\r\n");            break;
                case WL_NO_SSID_AVAIL:  modem_to_rx("\r\nWIFI NO SSID\r\n");         break;
                case WL_CONNECTED:      modem_to_rx("\r\nWIFI CONNECTED\r\n");        break;
                case WL_CONNECT_FAILED: modem_to_rx("\r\nWIFI CONNECT FAILED\r\n");  break;
                case WL_CONNECTION_LOST:modem_to_rx("\r\nWIFI CONNECTION LOST\r\n"); break;
                case WL_DISCONNECTED:   modem_to_rx("\r\nWIFI DISCONNECTED\r\n");     break;
                default:                modem_to_rx("\r\nWIFI NOT STARTED\r\n");      break;
                }
                break;
            }
            case '$':  // AT+W$ — IP address
                modem_to_rx("\r\n");
                modem_to_rx(WiFi.localIP().toString().c_str());
                modem_to_rx("\r\n");
                break;
            case '#':  // AT+W# — MAC address
                modem_to_rx("\r\n");
                modem_to_rx(WiFi.macAddress().c_str());
                modem_to_rx("\r\n");
                break;
            case '+':  // AT+W+ — reconnect
                WiFi.begin(modem_ssid, modem_pass[0] ? modem_pass : NULL);
                break;
            case '-':  // AT+W- — disconnect
                WiFi.disconnect();
                break;
            case '=': {  // AT+W=ssid,password
                char *comma = strchr(p, ',');
                if (comma) {
                    *comma = 0;
                    strncpy(modem_ssid, p, 63);
                    strncpy(modem_pass, comma + 1, 63);
                    p = comma + 1 + strlen(comma + 1);
                } else {
                    strncpy(modem_ssid, p, 63);
                    modem_pass[0] = 0;
                    p += strlen(p);
                }
                WiFi.begin(modem_ssid, modem_pass[0] ? modem_pass : NULL);
                break;
            }
            default:
                ok = false;
            }
            break;
        }

        default:
            ok = false;
        }
    }

    modem_response(ok ? RC_OK : RC_ERROR);
}

// =========================================================================================
// Public interface — called from cpm.h and abstraction_arduino.h
// =========================================================================================

// True if a byte is waiting in the RX buffer for the Z80 to read
bool modem_rx_available() {
    return !rx_empty();
}

// Read one byte from the RX buffer (call only when modem_rx_available() is true)
uint8 modem_read() {
    if (rx_empty()) return 0x1a;  // ^Z — CP/M EOF
    return rx_pop();
}

// True if the modem is ready to accept a TX byte from the Z80
bool modem_tx_ready() {
    if (modem_state == MODEM_ONLINE) return modem_client.connected();
    return true;  // command mode always accepts
}

// Z80 sends one byte to the modem (command or data mode)
void modem_write(uint8 ch) {
    if (modem_state == MODEM_ONLINE) {
        // ---- Data mode ----
        uint32 now      = millis();
        uint32 guard_ms = s_reg[S_GUARDTIME] * 20UL;

        if (ch == s_reg[S_ESCAPE]) {
            if (escape_count == 0 && (now - online_last_tx_ms) >= guard_ms) {
                // Sufficient silence before first + — start counting
                escape_count    = 1;
                escape_last_ms  = now;
            } else if (escape_count > 0 && (now - escape_last_ms) < guard_ms) {
                // Within guard window — keep counting
                escape_count++;
                escape_last_ms = now;
                if (escape_count >= 3) return;  // don't forward; wait for post-guard
            } else {
                escape_count = 0;
            }
        } else {
            escape_count = 0;
        }

        online_last_tx_ms = now;
        if (modem_client.connected()) modem_client.write(ch);

    } else if (modem_state == MODEM_COMMAND) {
        // ---- Command mode ----

        // Backspace
        if (ch == s_reg[S_BS] && cmd_len > 0) {
            cmd_len--;
            if (modem_echo) {
                rx_push(ch); rx_push(' '); rx_push(ch);   // back to Z80 via AUX RX
            }
            return;
        }

        // Carriage return — execute
        if (ch == s_reg[S_CR]) {
            cmd_buf[cmd_len] = 0;
            if (modem_echo) {
                rx_push('\r'); rx_push('\n');   // back to Z80
            }

            // A/ — repeat last command
            if (cmd_len >= 2 && cmd_buf[0] == 'A' && cmd_buf[1] == '/') {
                strncpy(cmd_buf, last_cmd, CMD_BUF_SIZE - 1);
                cmd_len = (uint8)strlen(cmd_buf);
            }

            if (cmd_len > 0) modem_process_command();
            cmd_len = 0;
            return;
        }

        if (ch == s_reg[S_LF]) return;  // ignore bare LF in command mode

        // Buffer the character — uppercase for AT parsing
        if (cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_len++] = (ch >= 'a' && ch <= 'z') ? (ch - 32) : ch;
            if (modem_echo) {
                rx_push(ch);   // echo back to Z80 via AUX RX
            }
        }
    }
}

// Line Status Register — 8250 compatible (UART_BASE+5)
uint8 modem_lsr() {
    uint8 lsr = 0;
    if (modem_rx_available()) lsr |= 0x01;  // bit 0 — DR  (data ready)
    if (modem_tx_ready())     lsr |= 0x60;  // bit 5 — THRE, bit 6 — TEMT
    return lsr;
}

// Modem Status Register — 8250 compatible (UART_BASE+6)
// bit 7 = DCD — 1 while TCP connection is active
uint8 modem_msr() {
    uint8 msr = 0;
    if (modem_state == MODEM_ONLINE && modem_client.connected()) msr |= 0x80;
    return msr;
}

// =========================================================================================
// modem_init — call from setup() after SD card is ready
// =========================================================================================
static void modem_read_config();  // forward declaration

void modem_init() {
    // S-register defaults
    memset(s_reg, 0, sizeof(s_reg));
    s_reg[S_ESCAPE]    = '+';
    s_reg[S_CR]        = 13;
    s_reg[S_LF]        = 10;
    s_reg[S_BS]        = 8;
    s_reg[S_GUARDTIME] = 50;
    s_reg[S_LISTENPORT]= 23;

    cmd_len     = 0;
    cmd_buf[0]  = 0;
    last_cmd[0] = 0;

    modem_read_config();

    // --- Connect to WiFi ---
    bool connected = false;
    if (modem_ssid[0] != 0) {
        _puts("WiFi SSID        [\e[1m");
        _puts(modem_ssid);
        _puts("\e[0m]\r\n");
        _puts("WiFi connecting  [");

        WiFi.begin(modem_ssid, modem_pass[0] ? modem_pass : NULL);
        uint32 start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > 15000) break;
            delay(250);
            _putcon('.');
        }

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            _puts(" \e[1mOK\e[0m ]\r\n");
            _puts("WiFi IP          [\e[1m");
            _puts(WiFi.localIP().toString().c_str());
            _puts("\e[0m]\r\n");
        } else {
            _puts(" \e[1mFAILED\e[0m ]\r\n");
        }
    } else {
        _puts("WiFi             [\e[1mno SSID — edit MODEM.CFG\e[0m]\r\n");
    }

    // --- Start listen server ---
    if (connected && s_reg[S_LISTENPORT] > 0) {
        modem_listen_port = s_reg[S_LISTENPORT];
        modem_server = new WiFiServer(modem_listen_port);
        modem_server->begin();
        char portstr[8];
        snprintf(portstr, sizeof(portstr), "%u", modem_listen_port);
        _puts("Modem listen     [\e[1mTCP port ");
        _puts(portstr);
        _puts("\e[0m]\r\n");
    }

    modem_state = connected ? MODEM_COMMAND : MODEM_OFFLINE;
    _puts("Modem            [\e[1m");
    _puts(connected ? "COMMAND" : "OFFLINE");
    _puts("\e[0m]\r\n");
}

// =========================================================================================
// modem_update — call on every BIOS entry to keep the state machine alive
// =========================================================================================
void modem_update() {

    // --- Drain incoming TCP data into RX ring buffer ---
    if (modem_state == MODEM_ONLINE) {
        while (modem_client.available() && !rx_full()) {
            rx_push((uint8)modem_client.read());
        }

        // --- +++ guard time: confirm escape after 1 s silence ---
        if (escape_count >= 3) {
            uint32 guard_ms = s_reg[S_GUARDTIME] * 20UL;
            if ((millis() - escape_last_ms) >= guard_ms) {
                modem_enter_command_mode();
                return;
            }
        }
    }

    // --- Detect TCP disconnect ---
    if (modem_state == MODEM_ONLINE &&
        !modem_client.connected() && !modem_client.available()) {
        modem_hangup(true);  // sends NO CARRIER
        return;
    }

    // --- Check for incoming connections (only when idle in COMMAND mode) ---
    if (modem_state == MODEM_COMMAND && modem_server != NULL) {
        WiFiClient incoming = modem_server->accept();
        if (incoming) {
            if (s_reg[S_AUTOANSWER] > 0) {
                // Auto-answer immediately
                modem_client = incoming;
                modem_client.setNoDelay(true);
                modem_state       = MODEM_ONLINE;
                online_last_tx_ms = millis();
                escape_count      = 0;
                modem_response(RC_CONNECT);
            } else {
                // Ring — wait for ATA
                modem_pending        = incoming;
                s_reg[S_RINGCOUNT]   = 0;
                modem_state          = MODEM_RINGING;
                modem_response(RC_RING);
            }
        }
    }

    // --- RINGING: send periodic RING, check for caller drop, handle auto-answer ---
    if (modem_state == MODEM_RINGING) {
        static uint32 last_ring_ms = 0;

        if (!modem_pending.connected()) {
            // Caller gave up
            modem_state        = MODEM_COMMAND;
            s_reg[S_RINGCOUNT] = 0;
        } else if ((millis() - last_ring_ms) > 4000) {
            last_ring_ms = millis();
            s_reg[S_RINGCOUNT]++;
            modem_response(RC_RING);

            if (s_reg[S_AUTOANSWER] > 0 &&
                s_reg[S_RINGCOUNT] >= s_reg[S_AUTOANSWER]) {
                modem_client = modem_pending;
                modem_client.setNoDelay(true);
                modem_state       = MODEM_ONLINE;
                online_last_tx_ms = millis();
                escape_count      = 0;
                modem_response(RC_CONNECT);
            }
        }
    }
}

// =========================================================================================
// modem_read_config — read MODEM.CFG from SD card root
// Format: KEY=VALUE  one per line, CRLF or LF terminated
// Keys:   SSID, PASS, LISTEN
// =========================================================================================
static void modem_read_config() {
    modem_ssid[0] = 0;
    modem_pass[0] = 0;

    File32 f;
    if (!f.open("MODEM.CFG", FILE_READ)) {
        _puts("MODEM.CFG        [\e[1mnot found\e[0m]\r\n");
        return;
    }

    char line[128];
    while (f.available()) {
        uint8 len = 0;
        while (f.available() && len < 127) {
            char c = (char)f.read();
            if (c == '\n') break;
            if (c != '\r') line[len++] = c;
        }
        line[len] = 0;
        if (len == 0) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;

        char *key = line;
        char *val = eq + 1;

        if      (strcmp(key, "SSID")   == 0) strncpy(modem_ssid,        val, 63);
        else if (strcmp(key, "PASS")   == 0) strncpy(modem_pass,        val, 63);
        else if (strcmp(key, "LISTEN") == 0) s_reg[S_LISTENPORT] = (uint8)atoi(val);
    }
    f.close();
    _puts("MODEM.CFG        [\e[1mloaded\e[0m]\r\n");
}

#endif // MODEM_H
