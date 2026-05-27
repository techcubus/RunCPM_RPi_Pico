# AT Command Set — WiFi Modem Reference

This document covers the Hayes AT command set as extended for WiFi/TCP modems,
specifically tailored to what we are implementing for RunCPM on the Pico 2 W.
Commands marked **[IMPL]** are in our implementation. Commands marked **[REF]**
are documented for reference / compatibility but may be stubbed.

---

## 1. Command Syntax Rules

- All commands begin with the prefix `AT` (ATtention), except `A/` and `+++`
- Commands are **case-insensitive** in most implementations; uppercase is traditional
- Multiple commands may be concatenated on one line: `ATE0Q0V1`
- Commands are terminated by `<CR>` (0x0D)
- Maximum command line length: 80 characters
- Edited with `<BS>` (0x08) before sending
- The `A/` command repeats the last command immediately (no `AT` prefix, no `<CR>` needed)
- Commands that consume the rest of the line (D, +W=) must come last in a concatenated string

---

## 2. Core Commands

### AT — Attention / Test
```
AT
```
Does nothing except return `OK`. Used to test that the modem is alive and to
autobaud (some implementations set baud rate from the timing of `AT`).

**Response:** `OK`

---

### ATZ — Soft Reset
```
ATZ
```
Resets modem state to power-on defaults. Does **not** reset WiFi or disconnect
a TCP session in progress (use `ATH` first).

**Response:** `OK`

---

### AT&F — Factory Reset
```
AT&F
```
Restores all settings (S-registers, echo, verbosity, etc.) to factory defaults.
Equivalent to ATZ for our purposes.

**Response:** `OK`

---

### AT&W — Save Settings  [REF]
```
AT&W
```
Writes current settings to non-volatile storage (EEPROM / SD card). In our
implementation, settings are held in RAM and re-read from `MODEM.CFG` on boot;
`AT&W` may update `MODEM.CFG`.

**Response:** `OK`

---

### ATE — Echo Control  [IMPL]
```
ATE0    Echo OFF — characters typed are not echoed back
ATE1    Echo ON  — (default) characters typed are echoed back
```
Controls whether the modem echoes command characters back to the terminal while
in command mode. Turn echo off (`ATE0`) when the CP/M program handles its own
echoing.

**Response:** `OK`

---

### ATQ — Quiet Mode  [IMPL]
```
ATQ0    Responses enabled (default) — modem sends OK, CONNECT, etc.
ATQ1    Quiet mode — modem sends no responses at all
```

**Response:** `OK` (or nothing if ATQ1 just set)

---

### ATV — Verbose / Numeric Result Codes  [IMPL]
```
ATV0    Numeric codes: 0=OK, 1=CONNECT, 2=RING, 3=NO CARRIER, 4=ERROR
ATV1    Verbose codes: OK, CONNECT, RING, NO CARRIER, ERROR (default)
```

In verbose mode (V1) responses are terminated `<CR><LF>response<CR><LF>`.
In numeric mode (V0) responses are just the digit followed by `<CR>`.

**Response:** `OK`

---

### ATI — Identification  [IMPL]
```
ATI     or ATI0    Print modem model string
ATI1               Print firmware version
```
Returns a human-readable identification string. Useful for verifying the modem
is present and for configuration scripts.

**Example response:**
```
RunCPM WiFi Modem v1.0 (Pico 2 W)
OK
```

**Response:** identification string, then `OK`

---

### A/ — Repeat Last Command
```
A/
```
Immediately re-executes the previous AT command line. No `AT` prefix, no `<CR>`
required. Useful for redialing.

---

## 3. Connection Commands

### ATDT — Dial (TCP Connect)  [IMPL]
```
ATDT hostname:port
ATDT ip.address:port
ATDT hostname              (defaults to port 23)
```

Opens a TCP connection to the specified host and port. `DT` = Dial Tone (the
traditional Hayes command for tone dialing, repurposed for TCP). `DP` (pulse
dial) is also accepted as an alias.

**Host formats accepted:**
- Hostname: `ATDT bbs.example.com:23`
- IPv4: `ATDT 192.168.1.10:6502`
- Hostname with default port: `ATDT telehack.com`

**Flow:**
1. Modem resolves hostname (DNS) or parses IP
2. Attempts TCP `connect(host, port)`
3. On success: switches to data mode, sends `CONNECT`
4. On failure: stays in command mode, sends `NO CARRIER`

**Responses:**
- `CONNECT` — TCP connection established, now in data mode
- `NO CARRIER` — connection refused, host unreachable, or DNS failure
- `NO DIALTONE` — WiFi not connected (our extension)
- `ALREADY IN CALL` — already have an active TCP connection
- `ERROR` — missing or malformed hostname

**Example:**
```
ATDT telehack.com:23
CONNECT

Welcome to Telehack...
```

---

### ATH — Hang Up  [IMPL]
```
ATH     or ATH0    Close current TCP connection
```

Closes the active TCP socket and returns to command mode.

**Response:** `NO CARRIER` then `OK`
(Some implementations return just `OK`; we return `NO CARRIER` first to signal
the connection drop to any CP/M program monitoring DCD.)

---

### ATA — Answer  [IMPL]
```
ATA
```

When an incoming connection is waiting (modem has sent `RING`), `ATA` accepts
it and switches to data mode.

**Response:** `CONNECT` (or `NO CARRIER` if no incoming connection pending)

---

### ATO — Return to Online Data Mode  [IMPL]
```
ATO     or ATO0
```

After escaping to command mode with `+++`, `ATO` returns to data mode without
dropping the TCP connection. Only valid if a TCP connection is still active.

**Response:** `CONNECT` (then switches to data mode silently)

---

### +++ — Escape to Command Mode  [IMPL]

Not a command per se, but a special in-band sequence. When in data mode, sending
three `+` characters with a **guard time** of at least 1 second of silence before
and after causes the modem to drop back to command mode **without** dropping the
TCP connection.

**Timing requirement (Hayes guard time):**
```
<1 second silence>  +++  <1 second silence>
```

If data contains `+++` without the surrounding silence, it is passed through
transparently to the TCP stream.

**Response:** `OK` (modem is now in command mode; connection still active)

---

## 4. WiFi Configuration Commands

### AT+W? — WiFi Status  [IMPL]
```
AT+W?
```
Returns current WiFi connection status.

**Responses:**
```
WIFI NOT STARTED
WIFI IDLE
WIFI NO SSID
WIFI CONNECTED
WIFI CONNECT FAILED
WIFI CONNECTION LOST
WIFI DISCONNECTED
```

---

### AT+W= — Connect to Access Point  [IMPL]
```
AT+W=ssid,password
AT+W=OpenNetwork,
```
Sets SSID and password and attempts WiFi connection. Credentials are saved to
`MODEM.CFG` on the SD card. For open networks, leave password empty.

**Response:** `OK` then (after connecting) `WIFI CONNECTED`, or `WIFI CONNECT FAILED`

---

### AT+W$ — Show IP Address  [IMPL]
```
AT+W$
```
Returns the current IP address assigned by DHCP (or static configuration).

**Response:** `192.168.1.42` then `OK`

---

### AT+W# — Show MAC Address  [REF]
```
AT+W#
```
Returns the WiFi chip's MAC address.

**Response:** `AA:BB:CC:DD:EE:FF` then `OK`

---

### AT+W+ — Reconnect  [IMPL]
```
AT+W+
```
Attempts to reconnect to the last configured access point.

**Response:** `OK`

---

### AT+W- — Disconnect WiFi  [REF]
```
AT+W-
```
Disconnects from the current access point. Any active TCP connection is also
dropped first.

**Response:** `OK`

---

## 5. S-Registers

S-registers are numbered parameters stored in modem RAM. Access them with:

```
ATSn=value      Set register n to value
ATSn?           Query register n (returns current value)
```

These may also be chained: `ATS0=1S14=2323` sets S0=1 and S14=2323.

### S-Registers implemented

| Reg | Name | Range | Default | Purpose |
|-----|------|-------|---------|---------|
| S0  | Auto-answer rings | 0–255 | 0 | Number of RINGs before auto-answering. 0 = manual answer only. |
| S1  | Ring counter | 0–255 | 0 | Counts incoming RINGs since last command. Read-only in practice. |
| S2  | Escape character | 0–255 | 43 (`+`) | ASCII code of the escape character used in `+++`. |
| S3  | Carriage return | 0–127 | 13 | ASCII code of line terminator (CR). |
| S4  | Line feed | 0–127 | 10 | ASCII code of line feed. |
| S5  | Backspace | 0–32  | 8  | ASCII code of backspace in command mode. |
| S12 | Escape guard time | 0–255 | 50 | Guard time in 20ms units (default = 1 second). |
| S14 | Listen port | 0–65535 | 23 | TCP port the server listens on for incoming connections. |
| S15 | Telnet protocol | 0–1 | 0 | 0 = raw TCP, 1 = Telnet protocol (IAC negotiation). |

### S0 — Auto-answer example
```
ATS0=1          Answer after 1 RING
ATS0=0          Disable auto-answer (manual ATA required)
ATS0?           Query current value
```

### S14 — Listen port
```
ATS14=23        Listen for incoming connections on port 23 (telnet)
ATS14=6400      Listen on port 6400
ATS14=0         Disable incoming connection server
```

### S15 — Telnet protocol
```
ATS15=0         Raw TCP — no IAC processing (default, best for CP/M)
ATS15=1         Telnet — handles IAC option negotiation
```

For CP/M use, `S15=0` (raw TCP) is almost always correct. Telnet option
negotiation can confuse CP/M terminal programs that don't expect IAC bytes in
the data stream.

---

## 6. Flow Control

### AT&K — Hardware Flow Control  [REF]
```
AT&K0   Disable RTS/CTS hardware flow control (default)
AT&K1   Enable RTS/CTS hardware flow control
```

Hardware flow control is between the serial terminal and the modem. For RunCPM,
where the "serial port" is virtual (BIOS AUX or I/O ports in the Z80 emulator),
flow control is handled by the `B_AUXOST` / LSR THRE status bit. `AT&K0` is the
right default.

---

## 7. Result Codes — Complete Reference

### Standard verbose / numeric codes

| Numeric | Verbose      | Meaning |
|---------|--------------|---------|
| 0       | `OK`         | Command accepted and executed |
| 1       | `CONNECT`    | TCP connection established — entering data mode |
| 2       | `RING`       | Incoming TCP connection waiting |
| 3       | `NO CARRIER` | TCP connection closed or failed |
| 4       | `ERROR`      | Command syntax error or invalid parameter |
| 5       | `CONNECT 1200` | (historic — we can emit just `CONNECT`) |
| 6       | `NO DIALTONE`| WiFi not connected — cannot dial |
| 7       | `BUSY`       | Already in a call / connection in progress |
| 8       | `NO ANSWER`  | TCP connect timed out |

### Extended status codes (verbose only)

| Code | Meaning |
|------|---------|
| `WIFI NOT STARTED`    | WiFi hardware not initialised |
| `WIFI IDLE`           | WiFi started but not connected |
| `WIFI NO SSID`        | Configured SSID not found |
| `WIFI CONNECTED`      | WiFi up and connected to AP |
| `WIFI CONNECT FAILED` | Authentication failure or other error |
| `WIFI CONNECTION LOST`| Was connected, link dropped |
| `WIFI DISCONNECTED`   | Deliberately disconnected |
| `ALREADY IN CALL`     | ATDT issued while TCP session active |
| `HANGUP`              | Remote end closed the TCP connection |

---

## 8. Data Mode vs Command Mode

The modem is always in one of two states:

### Command mode
- All characters received from Z80 are interpreted as AT commands
- Modem echoes characters (if ATE1) and responds with result codes
- TCP socket may be open (if `ATO` is available to return)
- Entered at startup, after `ATH`, after `+++`, after `NO CARRIER`

### Data mode
- All characters received from Z80 are forwarded to the TCP socket
- All characters received from TCP socket are forwarded to Z80
- Modem sends no result codes, does no command parsing
- Exited via `+++` guard sequence (returns to command mode with connection intact)
- Exited via TCP connection drop (sends `NO CARRIER`, returns to command mode)

### State diagram
```
                    ┌─────────────────────────────┐
          Power on  │                             │
         ──────────►│      COMMAND MODE           │◄──────────┐
                    │                             │           │
                    └───────┬───────┬─────────────┘           │
                            │ATDT   │ATA / S0≥1               │
                            │       │+ RING                   │ATH
                            ▼       ▼                         │+++
                    ┌─────────────────────────────┐           │
                    │                             │           │
                    │       DATA MODE             ├───────────┘
                    │                             │
                    └─────────────────────────────┘
```

---

## 9. Telnet Protocol (Optional, S15=1)

When `ATS15=1`, the modem performs Telnet option negotiation (RFC 854/855) before
passing data through. This is needed for BBSes that expect a proper Telnet client.

### IAC byte handling
Telnet uses byte 0xFF (`IAC` — Interpret As Command) as an escape. In raw TCP
mode (`S15=0`), 0xFF bytes pass through unmodified. In Telnet mode (`S15=1`),
0xFF must be doubled to send a literal 0xFF, and the modem processes option
negotiation sequences.

### Options typically negotiated
| Option | Code | Modem will |
|--------|------|-----------|
| SGA (Suppress Go Ahead) | 3  | DO + WILL |
| ECHO                    | 1  | DO + WILL |
| Binary Transmission     | 0  | DON'T + WON'T |
| NAWS (Window Size)      | 31 | DO + WILL (sends 80×24) |
| Terminal Type           | 24 | DO + WILL (sends `VT100`) |

For CP/M use, keep `S15=0`. Most CP/M terminal programs do not speak Telnet and
will be confused by IAC sequences.

---

## 10. Speed Dial / Address Book  [IMPL]

Store frequently called destinations for quick access:

```
AT&Z0=bbs.retrobbs.org:23       Store in slot 0
AT&Z1=192.168.1.100:4000        Store in slot 1
AT&Z0?                           Query slot 0
ATDS0                            Dial slot 0
```

Up to 10 slots (0–9) stored in `MODEM.CFG` on SD card.

---

## 11. Example Session

```
AT                          ← test
OK
AT+W?                       ← check WiFi
WIFI CONNECTED
AT+W$                       ← get IP
192.168.1.55
OK
ATDT bbs.retrobbs.org:23   ← dial a BBS
CONNECT

Connected to RetroBBS...    ← BBS greeting arrives

(... use CP/M terminal program normally ...)

+++                         ← escape to command mode (1s silence each side)
OK
ATO                         ← return to data mode
CONNECT

ATH                         ← hang up
NO CARRIER
OK
```

---

## 12. Implementation Notes for RunCPM

### Command buffer
- 80 character maximum line length
- Echo each character back as typed (if ATE1)
- `<BS>` removes last character from buffer and erases it on terminal
- `<CR>` triggers command execution
- Ignore `<LF>` (0x0A) in command mode input

### Guard time implementation (+++ detection)
The 1-second guard time is checked using `millis()`:
```cpp
// In modem_write() data-mode path:
if (ch == escape_char) {    // default '+' = 0x2B
    plus_count++;
    last_plus_time = millis();
} else {
    plus_count = 0;
}
if (plus_count >= 3 && (millis() - last_plus_time) >= guard_ms) {
    // check 1 second of silence also preceded the first +
    enter_command_mode();
    plus_count = 0;
}
```

### DCD signalling to Z80
Data Carrier Detect (DCD) in MSR bit 7 must reflect TCP connection state:
- TCP connected → DCD = 1 (MSR bit 7 set)
- TCP disconnected → DCD = 0

MODEM7 and BYE poll DCD to detect that a call has dropped. If DCD never goes
high, they will not accept the connection. If it never goes low, they will not
detect a disconnect.

### Parsing ATDT destination
```
ATDT [host] [: port]

host = hostname (alphanumeric + . + - + _) or dotted IPv4
port = 1–65535 decimal
```
If no port is given, default to 23. Strip any leading spaces after `ATDT`.
Accept `ATDP` as synonym for `ATDT`.

---

## Sources

- [Hayes AT command set — Wikipedia](https://en.wikipedia.org/wiki/Hayes_AT_command_set)
- [VT132 AT Modem Operation Manual — The High Nibble](https://thehighnibble.com/vt132/operation/modem/)
- [RetroWiFiModem AT command set — mecparts/RetroWiFiModem (GitHub)](https://github.com/mecparts/RetroWiFiModem)
- [Hayes AT command reference — Computer Hope](https://www.computerhope.com/atcom.htm)
- [Basic Hayes AT command set — modemhelp.net](https://www.modemhelp.net/basicatcommand.shtml)
