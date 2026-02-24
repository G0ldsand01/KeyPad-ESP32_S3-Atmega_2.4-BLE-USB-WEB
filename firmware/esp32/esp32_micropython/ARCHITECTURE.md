# Architecture du firmware — Macropad ESP32-S3

Inspiré de [MacroPad (aayushchouhan24)](https://github.com/aayushchouhan24/MacroPad) :

- **Logique modulaire** — KeyMatrix, Encoder, HidOutput
- **Logique événementielle** — callbacks sur chaque entrée
- **HID standalone** — fonctionne sur tout appareil sans app (phone, PC)

## Structure des fichiers

```
esp32_micropython/
├── Config.h          # Pins, constantes, codes HID
├── KeyMatrix.h/cpp   # Scan matrice 5×4, debounce, répétition
├── Encoder.h/cpp     # Encodeur rotatif (volume) + bouton (mute)
├── HidOutput.h/cpp   # Envoi HID (BLE + USB)
└── esp32_micropython.ino  # Setup, loop, callbacks, BLE, UART, web
```

## Flux d’événements

```
KeyMatrix.scan()  → debounce → onKeyPress(row, col, pressed, isRepeat)
                           → HidOutput.sendKey(symbol, row, col)
                           → BLE ou USB HID

Encoder.update()  → onEncoderRotate(dir)  → HidOutput.sendVolumeUp/Down()
                 → onEncoderButton(pressed) → HidOutput.sendMute()
```

## Différence avec MacroPad (aayushchouhan24)

| MacroPad | Ce projet |
|----------|-----------|
| ESP32 envoie des événements bruts (index touche) | ESP32 envoie directement les rapports HID |
| App PC traduit les événements en touches | Aucune app requise |
| Ne fonctionne pas sur téléphone | Fonctionne sur téléphone, PC, etc. |

## Configuration

- **Keymap** : définie dans `KEYMAP` (chargée depuis `DEFAULT_KEYMAP` ou via l’interface web)
- **Config** : via Web Serial / Web Bluetooth (JSON)
