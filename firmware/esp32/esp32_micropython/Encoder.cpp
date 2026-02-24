/*
 * Encoder.cpp — Décodage Gray-code avec filtre anti-bruit
 * 2 deltas consécutifs identiques requis pour éviter les faux positifs
 * Reset quand idle pour éviter accumulation de bruit
 */
#include "Encoder.h"

// Table Gray-code: (prev<<2 | curr) → delta (-1, 0, +1)
static const int8_t ENC_TABLE[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

#define ENC_IDLE_RESET_MS 150   // Si pas de delta valide depuis X ms → reset (évite drift)

void Encoder::begin() {
    pinMode(ENC_CLK_PIN, INPUT_PULLUP);
    pinMode(ENC_DT_PIN, INPUT_PULLUP);
    pinMode(ENC_SW_PIN, INPUT_PULLUP);
    _lastState = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
    _lastDeltaTime = millis();
}

void Encoder::update() {
    unsigned long now = millis();

    // ─── Rotation (Gray-code) ─────────────────────────────────────────────
    uint8_t curr = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
    uint8_t idx = (_lastState << 2) | curr;
    int8_t delta = ENC_TABLE[idx & 0x0F];
    _lastState = curr;

    if (delta != 0) {
        _lastDeltaTime = now;
        // Filtre: 2 deltas consécutifs identiques = rotation réelle (pas bruit)
        if (delta == _pendingDelta) {
            _position += delta;
            _pendingDelta = 0;
        } else {
            _pendingDelta = delta;
        }
    } else {
        _pendingDelta = 0;  // Transition invalide → annuler le pending
    }

    // Reset si idle longtemps (évite accumulation de bruit résiduel)
    if ((now - _lastDeltaTime) > ENC_IDLE_RESET_MS) {
        _position = 0;
        _reportedPos = 0;
        _pendingDelta = 0;
    }

#if ENABLE_ENCODER_VOLUME
    // Envoyer 1 commande volume par cran
    int32_t diff = _position - _reportedPos;
    if (diff != 0 && (now - _lastVolumeSent) >= ENC_VOLUME_COOLDOWN_MS) {
        int8_t dir = (diff > 0) ? 1 : -1;
        _reportedPos += dir;
        _lastVolumeSent = now;

        if (_rotateCb) _rotateCb(dir, 1);
    }
#endif

    // ─── Bouton (debounce) ───────────────────────────────────────────────
    bool raw = (digitalRead(ENC_SW_PIN) == LOW);
    if (raw != _btnPressed) {
        _btnPressed = raw;
        _btnLastChg = now;
    }
    if (_btnPressed != _btnStable && (now - _btnLastChg) >= 25) {
        _btnStable = _btnPressed;
        if (_buttonCb && _btnStable) _buttonCb(true);
    }
}
