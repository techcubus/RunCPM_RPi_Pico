// =========================================================================================
// Raspberry Pi Pico 2 W board definition
// RP2350 + CYW43 WiFi chip
//
// The Pico 2 W's onboard LED is connected to the CYW43 WiFi chip GPIO, not a
// direct RP2350 GPIO pin. arduino-pico maps LED_BUILTIN to the correct path
// transparently — always use LED_BUILTIN, never a raw GPIO number.
// =========================================================================================

// =========================================================================================
// Define SdFat as alias for SD
// =========================================================================================
SdFat SD;

// =========================================================================================
// Board identity
// =========================================================================================
#define LED    LED_BUILTIN   // CYW43 GPIO — handled transparently by arduino-pico
#define LEDinv 0
#define board_pico2w
#define board_analog_io
#define board_digital_io
#define BOARD "Raspberry Pi Pico 2 W"

// =========================================================================================
// SPIINIT — only used on ESP32 boards, kept here for structural compatibility
// =========================================================================================
#define SPIINIT     18, 16, 19, SS
#define SPIINIT_TXT "18,16,19,17"

// =========================================================================================
// SPI / SD pin assignments (SPI0, same physical pins as standard Pico)
//   MISO  GPIO 16  Pin 21
//   CS    GPIO 17  Pin 22
//   SCK   GPIO 18  Pin 24
//   MOSI  GPIO 19  Pin 25
// =========================================================================================
