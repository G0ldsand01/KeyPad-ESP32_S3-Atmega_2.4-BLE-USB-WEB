/*
 * Encoder.cpp — Implémentation encodeur
 */
#include "Encoder.h"

void Encoder::begin() {
    pinMode(ENC_CLK_PIN, INPUT_PULLUP);
    pinMode(ENC_DT_PIN, INPUT_PULLUP);
    pinMode(ENC_SW_PIN, INPUT_PULLUP);
    _lastClk = digitalRead(ENC_CLK_PIN);
}

void Encoder::update() {
    unsigned long now = millis();

    if (now - _lastRead < ENC_DEBOUNCE_MS) return;
    _lastRead = now;

    int clk = digitalRead(ENC_CLK_PIN);
    int dt = digitalRead(ENC_DT_PIN);

    if (clk != _lastClk) {
        _lastClk = clk;
        if (clk == 1) {
            // Inverser si le volume monte au lieu de descendre (broches CLK/DT ou sens)
            _position += (dt == 0) ? -1 : 1;
            _position = constrain(_position, -4, 4);
        }
    }

    if (now - _lastVolumeSent < VOLUME_COOLDOWN_MS) {
        // Bouton quand même
    } else if (now >= _blockedUntil) {
        if (now - _burstReset > ENC_BURST_WINDOW_MS) {
            _volUpCount = 0;
            _volDownCount = 0;
            _burstReset = now;
        }
        if (_volUpCount >= ENC_BURST_LIMIT || _volDownCount >= ENC_BURST_LIMIT) {
            _blockedUntil = now + ENC_BLOCK_MS;
            _position = 0;
            _volUpCount = 0;
            _volDownCount = 0;
        } else if (_position >= 2) {
            if (_rotateCb) _rotateCb(1);
            _position = 0;
            _lastVolumeSent = now;
            _volUpCount++;
            _volDownCount = 0;
        } else if (_position <= -2) {
            if (_rotateCb) _rotateCb(-1);
            _position = 0;
            _lastVolumeSent = now;
            _volDownCount++;
            _volUpCount = 0;
        }
    } else {
        _position = 0;
    }

    int sw = digitalRead(ENC_SW_PIN);
    if (sw == 0 && !_btnPressed && (now - _lastBtnPress > 200)) {
        _btnPressed = true;
        _lastBtnPress = now;
        if (_buttonCb) _buttonCb(true);
    } else if (sw == 1) {
        _btnPressed = false;
    }
}
