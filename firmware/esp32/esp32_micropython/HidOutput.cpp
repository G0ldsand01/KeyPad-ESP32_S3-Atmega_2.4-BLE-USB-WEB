/*
 * HidOutput.cpp — Envoi HID BLE + USB
 * Support complet: lettres, chiffres, symboles, touches nommées (ENTER, TAB, etc.)
 */
#include "HidOutput.h"
#include <BLEDevice.h>

// Codes HID Keyboard (Usage Page 0x07) — compatibles BLE et USB
#define HID_KB_A  0x04
#define HID_KB_B  0x05
#define HID_KB_C  0x06
#define HID_KB_D  0x07
#define HID_KB_E  0x08
#define HID_KB_F  0x09
#define HID_KB_G  0x0A
#define HID_KB_H  0x0B
#define HID_KB_I  0x0C
#define HID_KB_J  0x0D
#define HID_KB_K  0x0E
#define HID_KB_L  0x0F
#define HID_KB_M  0x10
#define HID_KB_N  0x11
#define HID_KB_O  0x12
#define HID_KB_P  0x13
#define HID_KB_Q  0x14
#define HID_KB_R  0x15
#define HID_KB_S  0x16
#define HID_KB_T  0x17
#define HID_KB_U  0x18
#define HID_KB_V  0x19
#define HID_KB_W  0x1A
#define HID_KB_X  0x1B
#define HID_KB_Y  0x1C
#define HID_KB_Z  0x1D
#define HID_KB_1  0x1E
#define HID_KB_2  0x1F
#define HID_KB_3  0x20
#define HID_KB_4  0x21
#define HID_KB_5  0x22
#define HID_KB_6  0x23
#define HID_KB_7  0x24
#define HID_KB_8  0x25
#define HID_KB_9  0x26
#define HID_KB_0  0x27
#define HID_KB_ENTER   0x28
#define HID_KB_ESC     0x29
#define HID_KB_BSPACE  0x2A
#define HID_KB_TAB     0x2B
#define HID_KB_SPACE   0x2C
#define HID_KB_MINUS   0x2D
#define HID_KB_EQUALS  0x2E
#define HID_KB_LBRACE  0x2F
#define HID_KB_RBRACE  0x30
#define HID_KB_BSLASH  0x31
#define HID_KB_SEMICOL 0x33
#define HID_KB_QUOTE   0x34
#define HID_KB_GRAVE   0x35
#define HID_KB_COMMA   0x36
#define HID_KB_DOT     0x37
#define HID_KB_SLASH   0x38
#define HID_KB_CAPSLOCK 0x39
#define HID_KB_LEFT    0x50
#define HID_KB_RIGHT   0x52
#define HID_KB_UP      0x53
#define HID_KB_DOWN    0x51

#define HID_MOD_SHIFT  0x02

struct KeycodeEntryMod {
    const char* symbol;
    uint8_t code;
    uint8_t modifier;
};

// Touches sans modificateur
static const KeycodeEntry NAMED_KEYS[] = {
    {"ENTER", HID_KB_ENTER}, {"TAB", HID_KB_TAB}, {"BACKSPACE", HID_KB_BSPACE},
    {"ESC", HID_KB_ESC}, {"ESCAPE", HID_KB_ESC}, {"SPACE", HID_KB_SPACE},
    {"DELETE", 0x4C}, {"UP", HID_KB_UP}, {"DOWN", HID_KB_DOWN},
    {"LEFT", HID_KB_LEFT}, {"RIGHT", HID_KB_RIGHT},
    {"1", HID_KB_1}, {"2", HID_KB_2}, {"3", HID_KB_3}, {"4", HID_KB_4}, {"5", HID_KB_5},
    {"6", HID_KB_6}, {"7", HID_KB_7}, {"8", HID_KB_8}, {"9", HID_KB_9}, {"0", HID_KB_0},
    {".", HID_KB_DOT}, {",", HID_KB_COMMA}, {"=", HID_KB_EQUALS}, {"-", HID_KB_MINUS},
    {"+", HID_KP_PLUS}, {"/", HID_KP_SLASH}, {"*", HID_KP_ASTERISK},
    {"[", HID_KB_LBRACE}, {"]", HID_KB_RBRACE}, {"\\", HID_KB_BSLASH},
    {";", HID_KB_SEMICOL}, {"'", HID_KB_QUOTE}, {"`", HID_KB_GRAVE}
};

// Touches avec Shift (symboles accessibles via Shift+digit/lettre)
static const KeycodeEntryMod NAMED_KEYS_SHIFT[] = {
    {"!", HID_KB_1, HID_MOD_SHIFT}, {"@", HID_KB_2, HID_MOD_SHIFT}, {"#", HID_KB_3, HID_MOD_SHIFT},
    {"$", HID_KB_4, HID_MOD_SHIFT}, {"%", HID_KB_5, HID_MOD_SHIFT}, {"^", HID_KB_6, HID_MOD_SHIFT},
    {"&", HID_KB_7, HID_MOD_SHIFT}, {"(", HID_KB_9, HID_MOD_SHIFT}, {")", HID_KB_0, HID_MOD_SHIFT},
    {"_", HID_KB_MINUS, HID_MOD_SHIFT},
    {"{", HID_KB_LBRACE, HID_MOD_SHIFT}, {"}", HID_KB_RBRACE, HID_MOD_SHIFT},
    {"|", HID_KB_BSLASH, HID_MOD_SHIFT}, {":", HID_KB_SEMICOL, HID_MOD_SHIFT},
    {"\"", HID_KB_QUOTE, HID_MOD_SHIFT}, {"~", HID_KB_GRAVE, HID_MOD_SHIFT},
    {"<", HID_KB_COMMA, HID_MOD_SHIFT}, {">", HID_KB_DOT, HID_MOD_SHIFT}, {"?", HID_KB_SLASH, HID_MOD_SHIFT}
};

static const int NUM_NAMED = sizeof(NAMED_KEYS) / sizeof(NAMED_KEYS[0]);
static const int NUM_NAMED_SHIFT = sizeof(NAMED_KEYS_SHIFT) / sizeof(NAMED_KEYS_SHIFT[0]);

void HidOutput::begin(USBHIDKeyboard* keyboard, USBHIDConsumerControl* consumer) {
    _keyboard = keyboard;
    _consumer = consumer;
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
    KeycodeResult r;
    if (getKeycodeAndModifier(symbol, &r)) return r.code;
    return 0;
}

bool HidOutput::getKeycodeAndModifier(const String& symbol, KeycodeResult* out) {
    if (!out || symbol.length() == 0) return false;
    out->code = 0;
    out->modifier = 0;

    // Touches avec Shift (vérifier d'abord pour éviter conflit avec "+")
    for (int i = 0; i < NUM_NAMED_SHIFT; i++) {
        if (symbol.equals(NAMED_KEYS_SHIFT[i].symbol)) {
            out->code = NAMED_KEYS_SHIFT[i].code;
            out->modifier = NAMED_KEYS_SHIFT[i].modifier;
            return true;
        }
    }
    // Touches sans modificateur
    for (int i = 0; i < NUM_NAMED; i++) {
        if (symbol.equals(NAMED_KEYS[i].symbol)) {
            out->code = NAMED_KEYS[i].code;
            return true;
        }
    }

    // Lettres minuscules a-z
    if (symbol.length() == 1) {
        char c = symbol.charAt(0);
        if (c >= 'a' && c <= 'z') {
            out->code = HID_KB_A + (c - 'a');
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            out->code = HID_KB_A + (c - 'A');
            out->modifier = HID_MOD_SHIFT;
            return true;
        }
        if (c >= '0' && c <= '9') {
            out->code = (c == '0') ? HID_KB_0 : HID_KB_1 + (c - '1');
            return true;
        }
        if (c == ' ') {
            out->code = HID_KB_SPACE;
            return true;
        }
    }
    return false;
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
        // USB: 0x81 = Left Shift modifier, 0x88+kc = raw key
        if (modifier & HID_MOD_SHIFT) {
            _keyboard->press(0x81);
            delay(5);
        }
        uint8_t usbCode = kc + HID_USB_RAW_OFFSET;
        _keyboard->press(usbCode);
        delay(10);
        _keyboard->release(usbCode);
        if (modifier & HID_MOD_SHIFT) {
            _keyboard->release(0x81);
        }
    }
}

void HidOutput::_sendConsumerReport(uint16_t code) {
    if (_bleConnected && _pInput != nullptr) {
        uint8_t kc = 0;
        if (code == CONSUMER_VOL_UP) kc = HID_KB_VOL_UP;
        else if (code == CONSUMER_VOL_DOWN) kc = HID_KB_VOL_DOWN;
        else if (code == CONSUMER_MUTE) kc = HID_KB_MUTE;

        if (kc != 0) {
            static unsigned long lastBleVolSent = 0;
            unsigned long now = millis();
            if ((now - lastBleVolSent) < BLE_VOLUME_STEP_DELAY_MS) return;
            lastBleVolSent = now;
            _sendKeypadReport(kc, 0);
            return;
        }
        uint8_t report[3] = {0x02, (uint8_t)(code & 0xFF), (uint8_t)(code >> 8)};
        _pInput->setValue(report, 3);
        delay(5);
        _pInput->notify();
        delay(50);
        uint8_t release[3] = {0x02, 0x00, 0x00};
        _pInput->setValue(release, 3);
        delay(5);
        _pInput->notify();
    } else if (_consumer != nullptr) {
        _consumer->press(code);
        delay(30);
        _consumer->release();
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

    KeycodeResult r;
    if (getKeycodeAndModifier(symbol, &r) && r.code > 0) {
        _sendKeypadReport(r.code, r.modifier);
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
