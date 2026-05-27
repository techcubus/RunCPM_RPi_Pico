# Raspberry Pi Pico 2 W — WiFi Programming Reference

## Hardware Overview

| Item | Detail |
|------|--------|
| MCU | RP2350 (dual Cortex-M33 or dual RISC-V Hazard3, 150 MHz default) |
| WiFi chip | CYW43439 (same family as Pico W's CYW43439) |
| WiFi standard | 802.11 b/g/n (2.4 GHz) |
| Security | WPA / WPA2 Personal |
| TCP/IP stack | lwIP (runs in background via DMA/PIO) |
| Onboard LED | Wired through CYW43 chip, **not** a direct GPIO |
| Board package | earlephilhower/arduino-pico |

The Pico 2 W is electrically and software-compatible with the original Pico W for WiFi purposes. Code written for Pico W compiles unchanged for Pico 2 W — the board package handles the RP2040 vs RP2350 difference transparently.

---

## 1. Arduino IDE Setup

### Board manager URL
```
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

### Board selection
`Tools → Board → Raspberry Pi Pico/RP2040/RP2350 → Raspberry Pi Pico 2 W`

### First upload gotcha
The COM port may not appear in the port list. Enable **"Show all ports"** in the port selector before the board shows up on first flash.

---

## 2. Resource Costs

Adding WiFi to a sketch is not free:

| Resource | Cost |
|----------|------|
| Flash    | +220 KB (CYW43 firmware blob, always present when WiFi board selected) |
| RAM      | ~40 KB (lwIP buffers, pre-allocated at startup) |

On the Pico 2 W: 4 MB flash, 520 KB RAM — easily accommodates these alongside RunCPM's 64 KB emulated RAM + program code.

---

## 3. LED Handling

The onboard LED on Pico W / Pico 2 W is connected to a GPIO pin on the CYW43 chip, **not** a RP2350 GPIO. Using the raw GPIO number directly will not work.

**Correct approach** — always use `LED_BUILTIN`:
```cpp
pinMode(LED_BUILTIN, OUTPUT);
digitalWrite(LED_BUILTIN, HIGH);   // works on all Pico variants
```

The arduino-pico library maps `LED_BUILTIN` to the correct path (GPIO 25 on standard Pico/Pico 2, CYW43 GPIO 0 on Pico W/Pico 2 W) transparently.

For RunCPM's `hardware/pico/pico2w_sd_spi.h`, define:
```cpp
#define LED LED_BUILTIN
#define LEDinv 0
```

---

## 4. WiFi Connection

### Include
```cpp
#include <WiFi.h>
```

### Connecting to an access point
```cpp
WiFi.begin(ssid, password);

while (WiFi.status() != WL_CONNECTED) {
    delay(500);
}
// Now connected — get IP:
IPAddress ip = WiFi.localIP();
```

### WiFi status values

| Constant | Value | Meaning |
|----------|-------|---------|
| `WL_IDLE_STATUS`     | 0 | WiFi not started |
| `WL_NO_SSID_AVAIL`   | 1 | SSID not found |
| `WL_SCAN_COMPLETED`  | 2 | Scan done |
| `WL_CONNECTED`       | 3 | Connected |
| `WL_CONNECT_FAILED`  | 4 | Wrong password / auth failure |
| `WL_CONNECTION_LOST` | 5 | Connection dropped |
| `WL_DISCONNECTED`    | 6 | Not connected |

### Connection check (non-blocking, call periodically)
```cpp
if (WiFi.status() != WL_CONNECTED) {
    // handle reconnect
}
```

### Static IP (optional)
```cpp
IPAddress local(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
WiFi.config(local, gateway, subnet);
WiFi.begin(ssid, password);
```

---

## 5. WiFiClient — Outgoing TCP Connections

`WiFiClient` makes outgoing TCP connections (i.e., the Pico 2 W is the client).

### Include and declare
```cpp
#include <WiFi.h>
WiFiClient client;
```

### Connect to a host
```cpp
// By hostname (DNS resolved automatically):
bool ok = client.connect("telnetbbs.example.com", 23);

// By IP address:
IPAddress addr(192, 168, 1, 10);
bool ok = client.connect(addr, 23);

// ok == true  → connected
// ok == false → failed (host unreachable, refused, timeout)
```

### Check connection state
```cpp
if (client.connected()) { ... }
```
Note: `connected()` returns true while the connection is up **or** while there is still unread data buffered, even after the remote end has closed. Use `available()` to distinguish.

### Send data (Z80 → TCP)
```cpp
client.write(byte_value);            // single byte
client.write(buf, len);              // buffer
client.print("hello\r\n");           // string
```

`write()` returns the number of bytes accepted. Returns 0 on failure.

### Receive data (TCP → Z80) — non-blocking
```cpp
int n = client.available();   // bytes waiting in RX buffer (0 if none)
if (n > 0) {
    int ch = client.read();   // returns -1 if no data (shouldn't happen after available() > 0)
    uint8_t buf[64];
    int got = client.read(buf, sizeof(buf));  // bulk read
}

int ch = client.peek();       // look at next byte without consuming it
```

### Disconnect
```cpp
client.stop();           // close connection, release socket
```

### Performance tuning
```cpp
client.setNoDelay(true);    // disable Nagle's algorithm
                            // sends each write() immediately without batching
                            // important for interactive terminal use
```

For a modem emulator, always set `setNoDelay(true)`. Nagle's algorithm introduces 200ms+ delays on small writes (individual characters), which is unacceptable for interactive CP/M terminal use.

### Full non-blocking polling pattern (use in modem_update())
```cpp
void modem_update() {
    // 1. Handle incoming TCP → RX ring buffer
    while (client.available() && !rx_buffer_full()) {
        rx_buffer_push(client.read());
    }

    // 2. Detect remote disconnect
    if (!client.connected() && !client.available()) {
        // connection dropped
        client.stop();
        modem_state = MODEM_COMMAND;
        modem_send_response("NO CARRIER");
    }
}
```

---

## 6. WiFiServer — Incoming TCP Connections

`WiFiServer` listens for incoming connections (i.e., Pico 2 W acts as server). Used for the auto-answer / incoming call feature.

### Declare and start
```cpp
WiFiServer server(23);    // listen on TCP port 23 (telnet)

void setup() {
    // ... WiFi.begin() and wait for connection ...
    server.begin();
}
```

### Accept incoming connections (non-blocking, call in loop)
```cpp
WiFiClient incoming = server.accept();
if (incoming) {
    // a new client connected
    incoming.setNoDelay(true);
    client = incoming;    // store as active client
}
```

`server.accept()` returns immediately — returns a falsy `WiFiClient` if nobody connected, a valid client if someone did.

### Listening port for RING / auto-answer
```cpp
// In modem_update(), check for incoming connections when not already connected:
if (modem_state == MODEM_COMMAND && !client.connected()) {
    WiFiClient incoming = server.accept();
    if (incoming) {
        if (s0_register > 0) {
            // auto-answer
            client = incoming;
            client.setNoDelay(true);
            modem_state = MODEM_ONLINE;
            modem_send_response("CONNECT");
        } else {
            // ring and wait for ATA
            pending_client = incoming;
            ring_count = 0;
            modem_state = MODEM_RINGING;
            modem_send_response("RING");
        }
    }
}
```

---

## 7. Threading / Core Model

**Important constraint:** In bare-metal Arduino mode (no FreeRTOS), WiFi code **must run on core 0**. The entire RunCPM emulator loop runs on core 0 (it's all in `setup()` and `loop()`), so this is automatically satisfied.

The lwIP TCP/IP stack processes packets in the background via RP2350 hardware (DMA / PIO driving the CYW43). This means:
- `client.available()` may return a new non-zero value at any time, even mid-Z80-instruction
- No explicit polling of the WiFi hardware is required
- You only need to call `client.available()` / `client.read()` / `client.write()` to interact with the buffered data

---

## 8. Reading WiFi Credentials from SD Card

Since WiFi credentials should not be compiled into firmware, read them from a plain text file on the SD card at startup.

### Config file format (`MODEM.CFG` at SD card root)
```
SSID=MyNetwork
PASS=MyPassword
LISTEN=23
```

### Parsing code (call before WiFi.begin())
```cpp
char wifi_ssid[64] = {0};
char wifi_pass[64] = {0};
uint16_t listen_port = 23;

void read_modem_config() {
    File32 f;
    if (!f.open("MODEM.CFG", FILE_READ)) return;

    char line[128];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < 127) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[len++] = c;
        }
        line[len] = 0;
        if (strncmp(line, "SSID=", 5) == 0) strncpy(wifi_ssid, line+5, 63);
        if (strncmp(line, "PASS=", 5) == 0) strncpy(wifi_pass, line+5, 63);
        if (strncmp(line, "LISTEN=", 7) == 0) listen_port = atoi(line+7);
    }
    f.close();
}
```

---

## 9. Complete Sketch WiFi Init Pattern

```cpp
#include <WiFi.h>
#include "modem.h"

void setup() {
    Serial.begin(115200);
    while (!Serial) { /* wait for USB */ }

    // SD card init first (needed to read MODEM.CFG)
    SPI.setRX(16); SPI.setCS(17); SPI.setSCK(18); SPI.setTX(19);
    SD.begin(SD_CONFIG);

    // Read WiFi credentials from SD card
    read_modem_config();

    // Init modem (connects to WiFi, starts server)
    modem_init();

    // ... rest of RunCPM boot (RAM, CCP, Z80 run loop) ...
}
```

---

## 10. Key API Summary

```cpp
// WiFi
WiFi.begin(ssid, pass)              → void
WiFi.status()                       → WL_CONNECTED etc.
WiFi.localIP()                      → IPAddress

// Client (outgoing)
WiFiClient client
client.connect(host, port)          → bool
client.connected()                  → bool
client.available()                  → int  (bytes waiting)
client.read()                       → int  (-1 if none)
client.read(buf, len)               → int  (bytes read)
client.peek()                       → int  (next byte, no consume)
client.write(byte)                  → size_t
client.write(buf, len)              → size_t
client.print(str)                   → size_t
client.stop()                       → void
client.setNoDelay(true)             → void  ← always do this

// Server (incoming)
WiFiServer server(port)
server.begin()                      → void
server.accept()                     → WiFiClient  (falsy if none)
```

---

## Sources

- [arduino-pico WiFi documentation](https://arduino-pico.readthedocs.io/en/latest/wifi.html)
- [arduino-pico WiFiClient API](https://arduino-pico.readthedocs.io/en/stable/wificlient.html)
- [Programming Raspberry Pi Pico 2 W with Arduino IDE — Random Nerd Tutorials](https://randomnerdtutorials.com/raspberry-pi-pico-2-w-arduino-ide/)
- [Creating a simple TCP Server with Raspberry Pi Pico W — VisualGDB](https://visualgdb.com/tutorials/raspberry/pico_w/)
