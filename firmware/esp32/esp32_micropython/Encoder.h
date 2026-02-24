/*
 * Encoder.h — Encodeur rotatif (volume) + bouton (mute)
 * Callbacks: onRotate(direction), onButton(pressed)
 */
#ifndef ENCODER_H
#define ENCODER_H

#include "Config.h"

class Encoder {
public:
    using RotateCallback = void (*)(int8_t direction);
    using ButtonCallback = void (*)(bool pressed);

    void begin();
    void update();

    void setRotateCallback(RotateCallback cb) { _rotateCb = cb; }
    void setButtonCallback(ButtonCallback cb) { _buttonCb = cb; }

private:
    RotateCallback _rotateCb = nullptr;
    ButtonCallback _buttonCb = nullptr;

    int _lastClk = 0;
    bool _btnPressed = false;
    unsigned long _lastRead = 0;
    unsigned long _lastVolumeSent = 0;
    unsigned long _lastBtnPress = 0;
    int _position = 0;
    int _volUpCount = 0;
    int _volDownCount = 0;
    unsigned long _burstReset = 0;
    unsigned long _blockedUntil = 0;
};

#endif // ENCODER_H
