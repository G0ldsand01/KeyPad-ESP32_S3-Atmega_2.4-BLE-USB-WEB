/*
 * KeyMatrix.h — Scan matrice 5×4 avec debounce et répétition
 * Callback onKey(row, col, pressed) pour logique événementielle
 */
#ifndef KEY_MATRIX_H
#define KEY_MATRIX_H

#include "Config.h"

class KeyMatrix {
public:
    // pressed=true, isRepeat=true = touche maintenue (répétition)
    using KeyCallback = void (*)(uint8_t row, uint8_t col, bool pressed, bool isRepeat);

    void begin();
    void scan();

    // État actuel d'une touche (pour détection combo PROFILE+0)
    bool isKeyPressed(uint8_t row, uint8_t col) const;

    void setCallback(KeyCallback cb) { _callback = cb; }
    void setDebounceMs(uint16_t ms) { _debounceMs = ms; }

private:
    KeyCallback _callback = nullptr;
    uint16_t _debounceMs = DEBOUNCE_MS;

    uint8_t _lastState[NUM_ROWS][NUM_COLS] = {0};
    unsigned long _lastChange[NUM_ROWS][NUM_COLS] = {0};
    unsigned long _lastRepeat[NUM_ROWS][NUM_COLS] = {0};
};

#endif // KEY_MATRIX_H
