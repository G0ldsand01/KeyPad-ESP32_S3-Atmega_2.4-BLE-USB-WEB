/*
 * Encoder.h — Encodeur rotatif (volume) + bouton (mute)
 * Inspiré de MacroPad (aayushchouhan24) — Gray-code, contrôle progressif
 *
 * Chaque cran = 1 commande volume (up ou down), pas de blocage.
 */
#ifndef ENCODER_H
#define ENCODER_H

#include "Config.h"

class Encoder {
public:
    // dir: +1 = monter, -1 = descendre. steps: nombre de crans (1 = 1 commande volume)
    using RotateCallback = void (*)(int8_t dir, uint8_t steps);
    using ButtonCallback = void (*)(bool pressed);

    void begin();
    void update();

    void setRotateCallback(RotateCallback cb) { _rotateCb = cb; }
    void setButtonCallback(ButtonCallback cb) { _buttonCb = cb; }
    void setSensitivity(uint8_t s) { _sensitivity = (s >= 1) ? s : 1; }

private:
    RotateCallback _rotateCb = nullptr;
    ButtonCallback _buttonCb = nullptr;
    uint8_t _sensitivity = 1;  // 1 cran = 1 commande volume

    uint8_t _lastState = 0;
    int8_t _pendingDelta = 0;   // Filtre: 2 deltas consécutifs identiques requis
    int32_t _position = 0;
    int32_t _reportedPos = 0;
    unsigned long _lastDeltaTime = 0;  // Détection idle → reset

    bool _btnPressed = false;
    bool _btnStable = false;
    unsigned long _btnLastChg = 0;
    unsigned long _lastVolumeSent = 0;
};

#endif // ENCODER_H
