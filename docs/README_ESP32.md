# Configuration ESP32-S3 pour Macropad

Guide de configuration et de programmation de l'ESP32-S3 pour le macropad avec MicroPython.

## 📦 Matériel

- **ESP32-S3** : Microcontrôleur principal
- **Connexions** : USB-C (wired), Bluetooth BLE HID
- **Communication** : UART avec ATmega328P (115200 bauds)
- **Encodeur rotatif** : Volume up/down et mute
- **Matrice de touches** : 5×4 (20 touches)

## 🔧 Configuration MicroPython

### Installation du firmware MicroPython

1. Téléchargez le firmware MicroPython pour ESP32-S3 depuis [micropython.org](https://micropython.org/download/esp32s3/)
2. Utilisez **esptool** pour flasher le firmware :
   ```bash
   esptool.py --chip esp32s3 --port COMx erase_flash
   esptool.py --chip esp32s3 --port COMx write_flash -z 0x0 firmware.bin
   ```
3. Vérifiez l'installation en ouvrant un terminal série (115200 bauds) et en tapant `help()`

### Installation des bibliothèques

Le firmware utilise les bibliothèques suivantes (incluses dans MicroPython ou à installer via `upip`) :

- **bluetooth** : Inclus dans MicroPython
- **machine** : Inclus dans MicroPython (Pin, PWM, UART)
- **adafruit_hid** : Pour USB HID (optionnel, nécessite firmware avec USB HID support)
- **json** : Inclus dans MicroPython
- **struct** : Inclus dans MicroPython

### Installation via upip (si nécessaire)

```python
import upip
upip.install('adafruit-circuitpython-bundle')
```

## 🔌 Brochage (Pins)

### Matrice de touches 5×4

**Colonnes (Cols)** - Sorties :
- Col 0 : GPIO 16
- Col 1 : GPIO 17
- Col 2 : GPIO 18
- Col 3 : GPIO 8

**Lignes (Rows)** - Entrées avec pull-up :
- Row 0 : GPIO 4
- Row 1 : GPIO 5
- Row 2 : GPIO 6
- Row 3 : GPIO 7
- Row 4 : GPIO 15

### Encodeur rotatif (Volume Control)

- **CLK** : GPIO 3
- **DT** : GPIO 46
- **SW** : GPIO 9 (bouton pour mute)

### Communication UART avec ATmega328P

- **TX** : GPIO 10 (ESP32 TX → ATmega RX)
- **RX** : GPIO 11 (ESP32 RX → ATmega TX)
- **Baudrate** : 115200

### LED PWM (Rétro-éclairage)

- **LED** : GPIO 2 (PWM)

## 📝 Structure du code

### Fichiers principaux

```
firmware/esp32_micropython/
└── main.py              # Fichier principal MicroPython
```

### Fonctionnalités principales

1. **Scan de matrice** : Détection des touches pressées
2. **BLE HID** : Émulation clavier Bluetooth
3. **USB HID** : Émulation clavier USB (si disponible)
4. **Encodeur rotatif** : Contrôle du volume (up/down/mute)
5. **Communication UART** : Échange de données avec ATmega
6. **Web UI** : Interface web via BLE Serial ou USB Serial
7. **OTA Updates** : Mise à jour du firmware via BLE

## 🔄 Communication avec ATmega328P

### Protocole UART

L'ESP32 communique avec l'ATmega via UART à 115200 bauds.

**Format des messages ESP32 → ATmega** :
- Commande `0x04` : Mise à jour de l'affichage
  - Format : `[0x04][profile_len][profile][output_len][output][keys_count][last_key_len][last_key][backlight_enabled][backlight_brightness]`

**Format des messages ATmega → ESP32** :
- `LIGHT=XXXX\n` : Niveau de luminosité (0-1023)
- Messages de debug ASCII

### Fonction `send_display_update_to_atmega()`

Envoie les données d'affichage à l'ATmega :
- Profil actuel
- Mode de connexion (USB/Bluetooth)
- Nombre de touches configurées
- Dernière touche appuyée
- État du rétro-éclairage

## 📡 Communication Web

### Interface Web UI

L'interface web est hébergée sur Vercel et communique avec l'ESP32 via :
- **Web Serial API** : Pour USB
- **Web Bluetooth API** : Pour BLE

### Format JSON

Les messages entre l'interface web et l'ESP32 utilisent JSON :

```json
{
  "type": "config",
  "keys": {
    "0-0": {"type": "key", "value": "PROFILE"},
    "0-1": {"type": "key", "value": "/"}
  },
  "activeProfile": "Profile 1",
  "outputMode": "bluetooth"
}
```

### Types de messages

- `config` : Configuration complète du macropad
- `keypress` : Notification de touche pressée
- `status` : Statut du système
- `ota_start` : Début de mise à jour OTA
- `ota_chunk` : Chunk de firmware pour OTA
- `ota_end` : Fin de mise à jour OTA

## 🎛️ Encodeur rotatif

### Fonctionnement

L'encodeur rotatif contrôle le volume :
- **Rotation horaire** : Volume up
- **Rotation anti-horaire** : Volume down
- **Bouton pressé** : Mute/unmute

### Implémentation

- Utilise la logique Gray Code pour détecter la rotation
- Compte les transitions pour détecter un "cran" (detent)
- Envoie les commandes Consumer Control HID (volume up/down/mute)

## 🔋 Gestion de la batterie

Si le macropad est alimenté par batterie :

1. L'ATmega lit le niveau de batterie via ADC
2. Les données sont envoyées à l'ESP32 via UART
3. L'ESP32 affiche les données sur l'interface web
4. L'ATmega affiche les données sur l'écran ST7789

## 🐛 Dépannage

### L'ESP32 ne se connecte pas

- Vérifiez les pilotes USB
- Utilisez un câble USB-C de données (pas seulement charge)
- Vérifiez que le port est correct dans le terminal série
- Vérifiez que le firmware MicroPython est correctement flashé

### BLE ne fonctionne pas

- Vérifiez que le Bluetooth est activé sur l'appareil
- Vérifiez que l'ESP32 est en mode advertising
- Redémarrez l'ESP32
- Vérifiez les logs dans le terminal série

### Les touches ne fonctionnent pas

- Vérifiez les connexions de la matrice
- Vérifiez que les pins sont correctement configurés
- Vérifiez les logs dans le terminal série
- Testez chaque touche individuellement

### UART avec ATmega ne fonctionne pas

- Vérifiez les connexions TX/RX (inversées entre ESP32 et ATmega)
- Vérifiez le baudrate (115200)
- Vérifiez que l'ATmega est alimenté
- Vérifiez les logs UART dans le terminal série

### USB HID ne fonctionne pas

- Vérifiez que le firmware MicroPython a le support USB HID
- Installez `adafruit-circuitpython-bundle` si nécessaire
- Vérifiez que `USB.begin()` est appelé avant `Keyboard.begin()`
- Redémarrez l'ESP32 après le téléversement

## 📚 Ressources

- [Documentation MicroPython ESP32-S3](https://docs.micropython.org/en/latest/esp32/quickref.html)
- [MicroPython Downloads](https://micropython.org/download/)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
- [Web Bluetooth API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API)

## 📝 Notes

- Le firmware utilise MicroPython au lieu d'Arduino
- BLE HID est toujours disponible
- USB HID nécessite un firmware MicroPython avec support USB HID
- La communication avec l'ATmega se fait via UART (pas I2C)
- L'interface web est hébergée sur Vercel
- Les mises à jour OTA sont possibles via BLE
- Le scan de matrice est fait en continu dans la boucle principale
- L'encodeur rotatif utilise la logique Gray Code pour une détection précise
