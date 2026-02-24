/*
 * KeyMatrix.cpp — Implémentation scan matrice
 */
#include "KeyMatrix.h"

bool KeyMatrix::isKeyPressed(uint8_t row, uint8_t col) const {
    if (row >= NUM_ROWS || col >= NUM_COLS) return false;
    return _lastState[row][col] != 0;
}

void KeyMatrix::begin() {
    for (int i = 0; i < NUM_COLS; i++) {
        pinMode(COL_PINS[i], OUTPUT);
        digitalWrite(COL_PINS[i], HIGH);
    }
    for (int i = 0; i < NUM_ROWS; i++) {
        pinMode(ROW_PINS[i], INPUT_PULLUP);
    }
}

void KeyMatrix::scan() {
    unsigned long now = millis();

    for (int c = 0; c < NUM_COLS; c++) {
        for (int i = 0; i < NUM_COLS; i++) {
            digitalWrite(COL_PINS[i], (i == c) ? LOW : HIGH);
        }
        delayMicroseconds(50);

        for (int r = 0; r < NUM_ROWS; r++) {
            int pressed = (digitalRead(ROW_PINS[r]) == 0) ? 1 : 0;

            if (pressed != _lastState[r][c]) {
                if (now - _lastChange[r][c] > _debounceMs) {
                    _lastChange[r][c] = now;
                    _lastState[r][c] = pressed;
                    _lastRepeat[r][c] = pressed ? now : 0;

                    if (_callback && pressed) {
                        _callback(r, c, true, false);
                    }
                }
            } else if (pressed && _lastState[r][c] && _callback) {
                unsigned long hold = now - _lastChange[r][c];
                unsigned long since = now - _lastRepeat[r][c];
                if (hold >= REPEAT_DELAY_MS && since >= REPEAT_INTERVAL_MS) {
                    _lastRepeat[r][c] = now;
                    _callback(r, c, true, true);
                }
            }
        }
    }

    for (int i = 0; i < NUM_COLS; i++) {
        digitalWrite(COL_PINS[i], HIGH);
    }
}
