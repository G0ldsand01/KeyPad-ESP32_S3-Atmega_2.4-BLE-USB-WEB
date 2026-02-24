/*
 * Config.h — Configuration centralisée du Macropad ESP32-S3
 * Inspiré de MacroPad (aayushchouhan24) — logique modulaire, HID standalone
 *
 * Le device fonctionne sur n'importe quel appareil (phone, PC) sans app.
 * Configurable via interface web (Web Serial / Web Bluetooth).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ─── Version ─────────────────────────────────────────────────────────────────
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

// ─── Matrice de touches 5×4 ─────────────────────────────────────────────────
#define NUM_ROWS 5
#define NUM_COLS 4
#define NUM_KEYS (NUM_ROWS * NUM_COLS)

static const uint8_t ROW_PINS[NUM_ROWS] = {4, 5, 6, 7, 15};   // R0..R4
static const uint8_t COL_PINS[NUM_COLS] = {16, 17, 18, 8};    // C0..C3

#define DEBOUNCE_MS 25
#define REPEAT_DELAY_MS 500
#define REPEAT_INTERVAL_MS 50

// ─── Encodeur rotatif ───────────────────────────────────────────────────────
#define ENC_CLK_PIN 3
#define ENC_DT_PIN 46
#define ENC_SW_PIN 9

#define ENC_DEBOUNCE_MS 5
#define VOLUME_COOLDOWN_MS 80
#define ENC_BURST_LIMIT 6
#define ENC_BURST_WINDOW_MS 400
#define ENC_BLOCK_MS 2000

// ─── UART ATmega ────────────────────────────────────────────────────────────
#define ATMEGA_UART_TX 10
#define ATMEGA_UART_RX 11
#define ATMEGA_UART_BAUD 9600

#define CMD_READ_LIGHT 0x01
#define CMD_SET_LED 0x02
#define CMD_GET_LED 0x03
#define CMD_UPDATE_DISPLAY 0x04
#define CMD_SET_DISPLAY_DATA 0x05
#define CMD_SET_DISPLAY_IMAGE 0x08
#define CMD_SET_DISPLAY_IMAGE_CHUNK 0x09
#define CMD_SET_ATMEGA_DEBUG 0x0A
#define CMD_SET_ATMEGA_LOG_LEVEL 0x0B

// ─── LEDs ───────────────────────────────────────────────────────────────────
#define LED_PWM_PIN 2
#define LED_STRIP_PIN 48
#define LED_STRIP_COUNT 17

// ─── Keymap par défaut (grille physique) ────────────────────────────────────
// [PROFILE] [/] [*] [-]
// [7] [8] [9] [+]
// [4] [5] [6]
// [1] [2] [3] [=]
// [0] [.]
// Défini dans le .ino principal

// ─── Codes HID Keypad (Usage Page 0x07) ──────────────────────────────────────
// BLE: envoi direct. USB: +0x88 pour bypass ASCII (non-printing key).
#define HID_USB_RAW_OFFSET 0x88

// Keypad 0-9, / * - + . =, flèches
#define HID_KP_1 0x59
#define HID_KP_2 0x5A
#define HID_KP_3 0x5B
#define HID_KP_4 0x5C
#define HID_KP_5 0x5D
#define HID_KP_6 0x5E
#define HID_KP_7 0x5F
#define HID_KP_8 0x60
#define HID_KP_9 0x61
#define HID_KP_0 0x62
#define HID_KP_SLASH 0x54
#define HID_KP_ASTERISK 0x55
#define HID_KP_MINUS 0x56
#define HID_KP_PLUS 0x57
#define HID_KP_DOT 0x63
#define HID_KP_EQUALS 0x67
#define HID_KP_LEFT 0x50
#define HID_KP_RIGHT 0x52
#define HID_KP_UP 0x53
#define HID_KP_DOWN 0x51

// Consumer Control (volume, media)
#define CONSUMER_VOL_UP 0xE9
#define CONSUMER_VOL_DOWN 0xEA
#define CONSUMER_MUTE 0xE2
#define CONSUMER_NEXT 0xB5
#define CONSUMER_PREV 0xB6
#define CONSUMER_PLAY_PAUSE 0xCD

// ─── BLE UUIDs ──────────────────────────────────────────────────────────────
#define BLE_SVC_HID "1812"
#define BLE_CHAR_INPUT "2A4D"
#define BLE_SVC_SERIAL "0000ffe0-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_SERIAL "0000ffe1-0000-1000-8000-00805f9b34fb"

// ─── Display update ─────────────────────────────────────────────────────────
#define DISPLAY_UPDATE_INTERVAL_MS 5000

#endif // CONFIG_H
