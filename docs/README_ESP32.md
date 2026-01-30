# Configuration ESP32-S3 pour Macropad

Guide de configuration et de programmation de l'ESP32-S3 pour le macropad.

## 📦 Matériel

- **ESP32-S3** : Microcontrôleur principal
- **Connexions** : USB-C (wired), Bluetooth, WiFi (futur avec dongle 2.4GHz)
- **Communication** : I2C avec ATmega328P/168A, I2C avec écran OLED, SPI avec capteur d'empreinte

## 🔧 Configuration Arduino IDE

### Installation du support ESP32

1. Ouvrez Arduino IDE
2. Allez dans **Fichier > Préférences**
3. Dans "URL de gestionnaire de cartes supplémentaires", ajoutez :
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Allez dans **Outils > Type de carte > Gestionnaire de cartes**
5. Recherchez "esp32" et installez "esp32 by Espressif Systems"

### Sélection de la carte

1. **Outils > Type de carte** : Sélectionnez "ESP32S3 Dev Module"
2. **Outils > Port** : Sélectionnez le port COM approprié
3. **Outils > USB CDC On Boot** : "Enabled"
4. **Outils > USB DFU On Boot** : "Disabled"
5. **Outils > USB Firmware MSC On Boot** : "Disabled"
6. **Outils > USB Mode** : "Hardware CDC and JTAG"
7. **Outils > CPU Frequency** : "240MHz (WiFi/BT)"
8. **Outils > Flash Size** : "4MB (32Mb)"
9. **Outils > Partition Scheme** : "Default 4MB with spiffs"
10. **Outils > PSRAM** : "OPI PSRAM" (si disponible)

## 📚 Librairies nécessaires

Installez les librairies suivantes via le Gestionnaire de bibliothèques (Croquis > Inclure une bibliothèque > Gérer les bibliothèques) :

### Librairies principales

1. **ArduinoJson** (par Benoit Blanchon)
   - **ESSENTIEL** : Pour le parsing et la sérialisation JSON
   - Recherchez : "ArduinoJson"
   - Version recommandée : 6.x

2. **ESP32 BLE Keyboard** (par Avinab Malla)
   - Pour l'émulation clavier HID via Bluetooth
   - Recherchez : "ESP32 BLE Keyboard"

3. **USB** (par Espressif Systems)
   - Inclus avec le support ESP32
   - Pour l'émulation clavier HID via USB

4. **Wire** (par Espressif Systems)
   - Inclus avec le support ESP32
   - Pour la communication I2C

5. **Adafruit GFX Library**
   - Pour le contrôle de l'écran OLED
   - Recherchez : "Adafruit GFX Library"

6. **Adafruit SSD1306**
   - Pour l'écran OLED 128×64
   - Recherchez : "Adafruit SSD1306"

7. **Adafruit Fingerprint Sensor Library**
   - Pour le capteur d'empreinte DFRobot SEN0348
   - Recherchez : "Adafruit Fingerprint Sensor Library"
   - **⚠️ IMPORTANT** : Sur ESP32-S3, **NE PAS utiliser SoftwareSerial**. Utilisez `HardwareSerial` (Serial1, Serial2, etc.) à la place. SoftwareSerial est conçu pour les microcontrôleurs AVR et ne fonctionne pas sur ESP32.

8. **Preferences** (par Espressif Systems)
   - Inclus avec le support ESP32
   - Pour la sauvegarde de configuration en mémoire flash

### Installation via Arduino IDE

1. Ouvrez Arduino IDE
2. Allez dans **Croquis > Inclure une bibliothèque > Gérer les bibliothèques**
3. Recherchez chaque librairie par son nom
4. Cliquez sur "Installer" pour chaque librairie

### Installation manuelle (si nécessaire)

Si une librairie n'est pas disponible via le gestionnaire :

1. Téléchargez le fichier ZIP de la librairie
2. Allez dans **Croquis > Inclure une bibliothèque > Ajouter la bibliothèque .ZIP**
3. Sélectionnez le fichier ZIP téléchargé

## 🔌 Brochage (Pins)

### Notes sur les Pins ESP32-S3-DevKitC-1

Selon la [documentation officielle ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1-v1.0.html), les pins disponibles sont :

#### Connecteur J1
- GPIO 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 46

#### Connecteur J3
- GPIO 0, 1, 2, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 47, 48

### ⚠️ Important : Conflits avec le port USB natif

**GPIO20 (USB_D+) et GPIO19 (USB_D-)** sont utilisés pour le port USB natif de l'ESP32-S3.

Si vous utilisez le port USB natif pour HID (clavier), **NE PAS utiliser GPIO19/20 pour I2C** car cela créerait un conflit.

#### Solution : Utiliser GPIO1 et GPIO2

Pour la communication I2C, nous utilisons :
- **SDA** : GPIO 1 (Pin 4 sur J3) ✅ Disponible, pas de conflit avec USB
- **SCL** : GPIO 2 (Pin 5 sur J3) ✅ Disponible, pas de conflit avec USB

#### Alternatives

Si GPIO1/2 sont occupés par d'autres périphériques :
- **Option 1** : GPIO21 (SDA) et GPIO4 (SCL)
- **Option 2** : GPIO4 (SDA) et GPIO5 (SCL)
- **Option 3** : Tout autre GPIO disponible sur J1 ou J3

#### Configuration actuelle

```cpp
#define I2C_SDA 1   // GPIO1 (Pin 4 sur J3) - Évite conflit avec USB_D+ (GPIO20)
#define I2C_SCL 2   // GPIO2 (Pin 5 sur J3) - Évite conflit avec USB_D- (GPIO19)
```

#### Pins USB natifs (à éviter pour I2C si utilisé)

- **GPIO20** : USB_D+ (Pin 19 sur J3)
- **GPIO19** : USB_D- (Pin 20 sur J3)

Ces pins sont utilisés par le port USB natif de l'ESP32-S3 pour HID.

### Communication I2C avec ATmega328P/168A

- **SDA** : GPIO 3 (marqué "Pin 8" sur le PCB) - **Testé et fonctionnel**
- **SCL** : GPIO 10 (marqué "Pin 9" sur le PCB) - **Testé et fonctionnel**
- **Fréquence I2C** : 100kHz (début), peut être augmentée à 400kHz
- **Résistances de pull-up** : 4.7kΩ à 3.3V sur chaque ligne (côté ATmega, PC4 et PC5)
- **⚠️ IMPORTANT** : GPIO20 (USB_D+) et GPIO19 (USB_D-) sont utilisés pour le port USB natif de l'ESP32-S3. Si vous utilisez ce port pour HID, **NE PAS** utiliser GPIO19/20 pour I2C.
- **Note** : Les pins marqués "8" et "9" sur le PCB correspondent aux GPIO3 et GPIO10. Ces pins ont été testés et fonctionnent correctement, et évitent tout conflit avec le port USB natif

### Écran OLED I2C

- **SDA** : GPIO 3 (partagé avec ATmega, marqué "Pin 8" sur le PCB)
- **SCL** : GPIO 10 (partagé avec ATmega, marqué "Pin 9" sur le PCB)
- **Adresse I2C** : 0x3C (par défaut)
- **Note** : Même bus I2C que l'ATmega, donc même adresse SDA/SCL

### Capteur d'empreinte digitale

- **RX** : GPIO 16 (HardwareSerial Serial1)
- **TX** : GPIO 17 (HardwareSerial Serial1)
- **Baudrate** : 57600
- **⚠️ IMPORTANT** : Sur ESP32-S3, utiliser `HardwareSerial` (Serial1, Serial2, etc.) au lieu de `SoftwareSerial`. SoftwareSerial n'est pas compatible avec ESP32-S3.

### Potentiomètre rotatif (Encodeur)

- **CLK** : GPIO 4
- **DT** : GPIO 5
- **SW** : GPIO 6 (bouton push)

### Capteur de lumière ambiante

- **SDA** : GPIO 3 (I2C partagé avec ATmega et OLED, marqué "Pin 8" sur le PCB)
- **SCL** : GPIO 10 (I2C partagé avec ATmega et OLED, marqué "Pin 9" sur le PCB)
- **Adresse I2C** : Variable selon le capteur

### Rétro-éclairage (LEDs)

- **Data** : GPIO 8 (WS2812B ou similaire)
- **Power** : GPIO 9 (optionnel)

## 📝 Structure du code

### Fichiers principaux

```
esp32_macropad/
├── esp32_macropad.ino      # Fichier principal
├── config.h                # Configuration et définitions
├── i2c_comm.h              # Communication I2C avec ATmega
├── display.h               # Gestion de l'écran OLED
├── fingerprint.h           # Gestion du capteur d'empreinte
├── encoder.h               # Gestion de l'encodeur rotatif
├── backlight.h             # Gestion du rétro-éclairage
└── profiles.h              # Gestion des profils
```

### Exemple de code minimal

```cpp
#include <Wire.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <Preferences.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// Configuration I2C
#define I2C_SDA 21
#define I2C_SCL 22

// Écran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Clavier USB
USBHIDKeyboard Keyboard;

// Préférences (sauvegarde)
Preferences preferences;

void setup() {
    Serial.begin(115200);
    
    // Initialiser USB HID
    USB.begin();
    Keyboard.begin();
    
    // Initialiser I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Initialiser écran OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Macropad Ready");
    display.display();
    
    // Initialiser préférences
    preferences.begin("macropad", false);
    
    Serial.println("ESP32-S3 Macropad initialized");
}

void loop() {
    // Lire les données depuis ATmega via I2C
    // Traiter les touches
    // Envoyer les commandes HID
    // Mettre à jour l'écran
    delay(10);
}
```

## 🔄 Communication avec ATmega328P/168A

### Protocole I2C

L'ESP32-S3 agit comme **maître I2C** et l'ATmega comme **esclave**.

**Adresse I2C de l'ATmega** : 0x08 (configurable)

### Format des messages

**Requête de l'ESP32 vers l'ATmega** :
- Demander l'état des touches (scan de la matrice)

**Réponse de l'ATmega vers l'ESP32** :
- Byte 0 : Masque de touches pressées (16 bits, 2 bytes)
- Byte 1 : Touche principale pressée (si applicable)

## 📡 Communication Web

### Format JSON

Les messages entre l'interface web et l'ESP32 utilisent JSON via Serial/Bluetooth :

```json
{
  "type": "config",
  "rows": 5,
  "cols": 4,
  "keys": {
    "0-1": {"type": "key", "value": "ENTER"},
    "1-0": {"type": "key", "value": "c", "modifiers": ["CTRL"]}
  },
  "activeProfile": "Profil 1",
  "profiles": {...},
  "outputMode": "usb"
}
```

### Types de messages

- `config` : Configuration complète du macropad
- `keypress` : Notification de touche pressée
- `status` : Statut du système
- `backlight` : Configuration du rétro-éclairage
- `fingerprint` : Commandes du capteur d'empreinte
- `display` : Configuration de l'écran
- `display_image` : Envoi d'image pour l'écran

## 🔋 Gestion de la batterie

Si le macropad est alimenté par batterie :

1. Connectez un ADC pour lire le niveau de batterie
2. Utilisez un diviseur de tension si nécessaire
3. Calculez le pourcentage de batterie
4. Affichez sur l'écran OLED
5. Envoyez les données à l'interface web

## 🐛 Dépannage

### L'ESP32 ne se connecte pas

- Vérifiez les pilotes USB
- Utilisez un câble USB-C de données (pas seulement charge)
- Vérifiez que le port est correct dans Arduino IDE

### I2C ne fonctionne pas

- Vérifiez les connexions SDA/SCL
- Vérifiez les résistances de pull-up (4.7kΩ recommandées)
- Utilisez un scanner I2C pour détecter les adresses
- **Vérifiez que vous n'utilisez pas GPIO19/20 si le port USB natif est utilisé**

### L'écran OLED ne s'affiche pas

- Vérifiez l'adresse I2C (0x3C ou 0x3D)
- Vérifiez les connexions
- Testez avec un exemple Adafruit SSD1306

### Le clavier HID ne fonctionne pas

- Vérifiez que USB CDC On Boot est activé
- Redémarrez l'ESP32 après le téléversement
- Vérifiez que Keyboard.begin() est appelé
- Vérifiez que USB.begin() est appelé AVANT Keyboard.begin()

## 📚 Ressources

- [Documentation ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [Arduino ESP32](https://github.com/espressif/arduino-esp32)
- [ESP32 BLE Keyboard](https://github.com/T-vK/ESP32-BLE-Keyboard)
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)

## 📝 Notes

- L'ESP32-S3 supporte nativement USB HID (pas besoin de bibliothèque externe pour USB)
- Pour Bluetooth, utilisez ESP32 BLE Keyboard
- Le WiFi sera activé avec le dongle 2.4GHz (futur)
- La configuration est sauvegardée en mémoire flash via Preferences
- Sur l'ESP32-S3, contrairement à l'ESP32 classique, les pins I2C peuvent être configurés sur **n'importe quel GPIO disponible**
- Il n'y a pas de pins I2C "dédiés" - tous les GPIO peuvent être utilisés pour I2C
- Les résistances de pull-up (4.7kΩ à 3.3V) sont toujours nécessaires sur les lignes SDA et SCL
- **⚠️ Attention** : Si vous utilisez le port USB natif (GPIO19/20) pour HID, ne les utilisez pas pour I2C
- GPIO22 n'est pas disponible sur cette carte (pas listé dans J1 ou J3)