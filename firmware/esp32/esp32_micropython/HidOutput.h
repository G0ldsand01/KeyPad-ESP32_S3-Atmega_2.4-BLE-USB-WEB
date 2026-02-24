/*
 * HidOutput.h — Envoi HID (BLE + USB)
 * Centralise la logique keypad + Consumer Control
 * Standalone : fonctionne sur tout appareil sans app
 */
#ifndef HID_OUTPUT_H
#define HID_OUTPUT_H

#include "Config.h"
#include <BLEDevice.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>

struct KeycodeEntry {
    const char* symbol;
    uint8_t code;
};

class HidOutput {
public:
    void begin(USBHIDKeyboard* keyboard, USBHIDConsumerControl* consumer = nullptr);
    void setBleState(bool connected, BLECharacteristic* pInput);

    void sendKey(const String& symbol, uint8_t row, uint8_t col);
    void sendVolumeUp();
    void sendVolumeDown();
    void sendMute();
    void sendConsumer(uint16_t code);

    static bool keyShouldRepeat(const String& symbol);
    static uint8_t getKeycode(const String& symbol);

private:
    USBHIDKeyboard* _keyboard = nullptr;
    USBHIDConsumerControl* _consumer = nullptr;
    bool _bleConnected = false;
    BLECharacteristic* _pInput = nullptr;

    void _sendKeypadReport(uint8_t kc, uint8_t modifier = 0);
    void _sendConsumerReport(uint16_t code);
};

#endif // HID_OUTPUT_H
