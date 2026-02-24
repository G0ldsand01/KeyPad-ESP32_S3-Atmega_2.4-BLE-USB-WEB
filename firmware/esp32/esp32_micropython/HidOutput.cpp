/*
 * HidOutput.cpp — Envoi HID BLE + USB
 */
#include "HidOutput.h"
#include <BLEDevice.h>

// Codes clavier standard (0x1E-0x27, 0x37) pour 0-9 et . — meilleure compatibilité Android/BLE
// Keypad (0x59-0x63) cause des caractères incorrects sur certains appareils
#define HID_KB_1 0x1E
#define HID_KB_2 0x1F
#define HID_KB_3 0x20
#define HID_KB_4 0x21
#define HID_KB_5 0x22
#define HID_KB_6 0x23
#define HID_KB_7 0x24
#define HID_KB_8 0x25
#define HID_KB_9 0x26
#define HID_KB_0 0x27
#define HID_KB_DOT 0x37
#define HID_KB_EQUALS 0x2E  // Keyboard = and + (évite confusion keypad 0x67 → 5)

static const KeycodeEntry KEYCODES[] = {
    {"1", HID_KB_1}, {"2", HID_KB_2}, {"3", HID_KB_3},
    {"4", HID_KB_4}, {"5", HID_KB_5}, {"6", HID_KB_6},
    {"7", HID_KB_7}, {"8", HID_KB_8}, {"9", HID_KB_9},
    {"0", HID_KB_0},
    {".", HID_KB_DOT},
    {"=", HID_KB_EQUALS},
    {"/", HID_KP_SLASH}, {"*", HID_KP_ASTERISK}, {"-", HID_KP_MINUS}, {"+", HID_KP_PLUS},
    {"LEFT", HID_KP_LEFT}, {"RIGHT", HID_KP_RIGHT}, {"UP", HID_KP_UP}, {"DOWN", HID_KP_DOWN}
};

static const int NUM_KEYCODES = sizeof(KEYCODES) / sizeof(KEYCODES[0]);

void HidOutput::begin(USBHIDKeyboard* keyboard) {
    _keyboard = keyboard;
}

void HidOutput::setBleState(bool connected, BLECharacteristic* pInput) {
    _bleConnected = connected;
    _pInput = pInput;
}

bool HidOutput::keyShouldRepeat(const String& symbol) {
    return symbol != "PROFILE" && symbol != "VOL_UP" && symbol != "VOL_DOWN" && symbol != "MUTE"
        && symbol != "Prev" && symbol != "Next" && symbol != "Select";
}

uint8_t HidOutput::getKeycode(const String& symbol) {
    for (int i = 0; i < NUM_KEYCODES; i++) {
        if (symbol == KEYCODES[i].symbol) return KEYCODES[i].code;
    }
    return 0;
}

void HidOutput::_sendKeypadReport(uint8_t kc, uint8_t modifier) {
    uint8_t release[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    if (_bleConnected && _pInput != nullptr) {
        _pInput->setValue(release, 9);
        delay(5);
        _pInput->notify();
        delay(5);
        uint8_t report[9] = {0x01, modifier, 0x00, kc, 0x00, 0x00, 0x00, 0x00, 0x00};
        _pInput->setValue(report, 9);
        delay(8);
        _pInput->notify();
        delay(5);
        for (int i = 0; i < 3; i++) {
            _pInput->setValue(release, 9);
            _pInput->notify();
            delay(5);
        }
    } else if (_keyboard != nullptr) {
        uint8_t usbCode = kc + HID_USB_RAW_OFFSET;
        _keyboard->press(usbCode);
        delay(10);
        _keyboard->release(usbCode);
    }
}

void HidOutput::_sendConsumerReport(uint16_t code) {
    if (_bleConnected && _pInput != nullptr) {
        uint8_t report[3] = {0x02, (uint8_t)(code & 0xFF), (uint8_t)(code >> 8)};
        _pInput->setValue(report, 3);
        delay(5);
        _pInput->notify();
        delay(50);
        uint8_t release[3] = {0x02, 0x00, 0x00};
        _pInput->setValue(release, 3);
        delay(5);
        _pInput->notify();
    }
}

void HidOutput::sendKey(const String& symbol, uint8_t row, uint8_t col) {
    if (symbol == "PROFILE") return;

    if (symbol == "VOL_UP") { sendVolumeUp(); return; }
    if (symbol == "VOL_DOWN") { sendVolumeDown(); return; }
    if (symbol == "MUTE") { sendMute(); return; }
    if (symbol == "Prev") { sendConsumer(CONSUMER_PREV); return; }
    if (symbol == "Next") { sendConsumer(CONSUMER_NEXT); return; }
    if (symbol == "Select") { sendConsumer(CONSUMER_PLAY_PAUSE); return; }

    uint8_t kc = getKeycode(symbol);
    if (kc > 0) {
        _sendKeypadReport(kc, 0);
    }
}

void HidOutput::sendVolumeUp() {
    _sendConsumerReport(CONSUMER_VOL_UP);
}

void HidOutput::sendVolumeDown() {
    _sendConsumerReport(CONSUMER_VOL_DOWN);
}

void HidOutput::sendMute() {
    _sendConsumerReport(CONSUMER_MUTE);
}

void HidOutput::sendConsumer(uint16_t code) {
    _sendConsumerReport(code);
}
