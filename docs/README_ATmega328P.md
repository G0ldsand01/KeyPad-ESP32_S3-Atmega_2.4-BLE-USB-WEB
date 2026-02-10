# Configuration ATmega328P pour Macropad

Guide de configuration et de programmation de l'ATmega328P pour le capteur de lumière, l'écran ST7789 et la communication UART avec l'ESP32-S3.

## 📦 Matériel

- **Microcontrôleur** : ATmega328P
- **Programmeur** : PICKit 4 (fourni par l'école)
- **IDE** : Microchip Studio (anciennement Atmel Studio)
- **Fonction** : Capteur de lumière (TEMT6000), écran ST7789 TFT (1.9" 170×320), LED PWM, communication UART avec ESP32-S3

## 🔧 Configuration Microchip Studio

### Installation

1. Téléchargez **Microchip Studio** depuis le site officiel de Microchip
2. Installez le logiciel avec les composants par défaut
3. Assurez-vous que les outils pour AVR sont inclus

### Configuration du projet

1. Ouvrez Microchip Studio
2. **File > New > Project**
3. Sélectionnez **GCC C++ Executable Project**
4. Choisissez **ATmega328P** comme device
5. Configurez le projet

### Configuration du compilateur

Dans **Project Properties > Toolchain > AVR/GNU C++ Compiler > Symbols** :
- Ajoutez : `F_CPU=16000000UL` (si pas déjà défini)

## 🔌 Brochage (Pins)

### Capteur de lumière TEMT6000

- **Signal** : ADC0 (PC0, Pin 23)
- **VCC** : 3.3V ou 5V
- **GND** : Masse
- **Note** : Le TEMT6000 est un phototransistor, connectez une résistance de pull-down (10kΩ) entre le signal et GND

### LED PWM (Rétro-éclairage)

- **LED** : OC0B (PD5, Pin 19) - PWM
- **Fréquence PWM** : ~1kHz (configurable)
- **Duty cycle** : 0-255 (contrôlé par l'ESP32)

### Communication UART avec ESP32-S3

- **RX** : PD0 (Pin 2) - Reçoit de l'ESP32
- **TX** : PD1 (Pin 3) - Envoie à l'ESP32
- **Baudrate** : 115200
- **⚠️ IMPORTANT** : Les pins TX/RX sont inversés entre ESP32 et ATmega :
  - ESP32 TX (GPIO 10) → ATmega RX (PD0)
  - ESP32 RX (GPIO 11) → ATmega TX (PD1)

### Écran ST7789 TFT (1.9" 170×320)

- **MOSI** : PB3 (Pin 17) - SPI Data
- **SCK** : PB5 (Pin 19) - SPI Clock
- **CS** : PB2 (Pin 16) - Chip Select
- **DC** : PB1 (Pin 15) - Data/Command
- **RST** : PB0 (Pin 14) - Reset (optionnel, peut être connecté à VCC)
- **VCC** : 3.3V
- **GND** : Masse
- **Backlight** : Contrôlé par LED PWM (PD5)

### Alimentation

- **VCC** : 5V ou 3.3V (selon configuration)
- **GND** : Masse
- **AVCC** : Connecté à VCC
- **AREF** : Peut être laissé flottant ou connecté à VCC

## 📚 Structure du code

### Fichier principal

```
firmware/atmega/atmega_light/
└── main.cpp              # Fichier principal
```

### Fonctionnalités principales

1. **Lecture ADC** : Capteur de lumière TEMT6000 (0-1023)
2. **Communication UART** : Échange de données avec ESP32
3. **Contrôle PWM** : LED de rétro-éclairage
4. **Affichage ST7789** : Écran TFT 1.9" 170×320
5. **Debug UART** : Messages de débogage vers ESP32

## 🔄 Communication UART avec ESP32-S3

### Protocole UART

L'ATmega communique avec l'ESP32 via UART à 115200 bauds.

### Commandes reçues (ESP32 → ATmega)

- **0x01** : Lire la luminosité (réponse : 2 bytes little-endian)
- **0x02** : Définir la luminosité LED (0-255)
- **0x03** : Obtenir la luminosité LED actuelle
- **0x04** : Mettre à jour l'affichage (profil, mode, touches, etc.)
- **0x08** : Commencer la réception d'une image RGB565
- **0x09** : Recevoir un chunk d'image
- **0x0A** : Activer/désactiver le debug UART
- **0x0B** : Définir le niveau de log

### Format de la commande 0x04 (Display Update)

```
[0x04][profile_len][profile][output_len][output][keys_count][last_key_len][last_key][backlight_enabled][backlight_brightness]
```

### Messages envoyés (ATmega → ESP32)

- **LIGHT=XXXX\n** : Niveau de luminosité (0-1023), envoyé toutes les 1 seconde
- **Messages de debug** : Si activé, messages ASCII pour débogage

## 🖥️ Affichage ST7789

### Configuration

- **Résolution** : 320×210 pixels (landscape, connecteur à droite)
- **Rotation** : 270° (MADCTL = 0xA0)
- **Format couleur** : RGB565
- **Inversion** : INVOFF (couleurs normales)
- **Ordre des bytes** : High byte puis Low byte

### Layout de l'affichage

L'écran affiche les informations suivantes :

1. **Profil actuel** : Nom du profil (ex: "PROFIL 1")
2. **Séparateur** : Ligne grise horizontale
3. **Mode de connexion** : "MODE DE CONNECTION : BLUETOOTH" ou "USB"
4. **Dernière touche** : "TOUCHE : {touche}" ou "AUCUNE"
5. **Touches configurées** : "TOUCHE CONFIGURE : X/20"
6. **Rétro-éclairage** : "RETRO-ECLAIRAGE : ON" ou "OFF"
7. **Luminosité** : "LUMINOSITE : XXXX" (0-1023)

### Design

- **Fond** : Noir (0x0000)
- **Rectangle intérieur** : Gris très foncé (0x1082) avec offset de 5px (côtés) et 30px (haut)
- **Bordures** : Gris clair (0x8410) sur les bords du rectangle intérieur
- **Texte** : Blanc (0xFFFF) sur fond gris très foncé
- **Séparateur** : Gris clair (0x8410)

### Optimisations

- Le fond noir n'est rempli qu'une seule fois (au démarrage)
- Seul le rectangle intérieur est redessiné lors des mises à jour
- Pas de refresh complet de l'écran à chaque mise à jour

## 💡 Contrôle de la LED

### PWM

La LED est contrôlée via PWM sur OC0B (PD5) :
- **Fréquence** : ~1kHz
- **Duty cycle** : 0-255 (contrôlé par l'ESP32)
- **Contrôle automatique** : Si `light_level < 200`, la LED s'active automatiquement

## 🔧 Configuration du compilateur

### Fuses (Fusibles)

Configuration recommandée pour ATmega328P :

- **Low Fuse** : 0xFF (Clock externe, pas de division)
- **High Fuse** : 0xDE (Boot reset, SPI enabled)
- **Extended Fuse** : 0xFD (Brown-out à 2.7V)

### Fréquence d'horloge

- **16 MHz** : Utilisez un cristal externe 16MHz
- **8 MHz** : Si vous utilisez l'oscillateur interne

### Watchdog Timer

Le watchdog timer est désactivé au démarrage pour éviter les resets intempestifs :
```c
#include <avr/wdt.h>
wdt_disable();
```

## 📝 Programmation avec PICKit 4

### Connexions PICKit 4

1. Connectez le PICKit 4 à l'ordinateur via USB
2. Connectez les broches :
   - **VDD** : Alimentation (5V ou 3.3V)
   - **GND** : Masse
   - **PGC** : Pin de programmation clock
   - **PGD** : Pin de programmation data
   - **MCLR** : Reset

### Étapes de programmation

1. Ouvrez Microchip Studio
2. **Tools > Device Programming**
3. Sélectionnez **PICKit 4** comme tool
4. Sélectionnez **ATmega328P** comme device
5. Cliquez sur **Apply**
6. Vérifiez les fuses si nécessaire
7. **Memories > Flash** : Sélectionnez le fichier .hex
8. Cliquez sur **Program**

## 🐛 Dépannage

### L'ATmega ne répond pas sur UART

- Vérifiez les connexions TX/RX (inversées entre ESP32 et ATmega)
- Vérifiez le baudrate (115200)
- Vérifiez que l'ATmega est alimenté correctement
- Vérifiez que l'UART est correctement initialisé
- Vérifiez les logs dans le terminal série de l'ESP32

### L'écran ST7789 ne s'affiche pas

- Vérifiez les connexions SPI (MOSI, SCK, CS, DC, RST)
- Vérifiez l'alimentation (3.3V)
- Vérifiez que le backlight est activé
- Vérifiez la rotation (MADCTL = 0xA0)
- Vérifiez l'inversion des couleurs (INVOFF)
- Vérifiez l'ordre des bytes (high puis low)

### Le capteur de lumière ne fonctionne pas

- Vérifiez la connexion ADC0 (PC0)
- Vérifiez la résistance de pull-down (10kΩ)
- Vérifiez l'alimentation du TEMT6000
- Vérifiez que l'ADC est correctement initialisé

### La LED PWM ne fonctionne pas

- Vérifiez la connexion PD5 (OC0B)
- Vérifiez que le timer 0 est correctement configuré
- Vérifiez que la LED est correctement connectée
- Vérifiez que le duty cycle est défini (0-255)

### Le PICKit 4 ne détecte pas l'ATmega

- Vérifiez toutes les connexions
- Vérifiez que l'ATmega est alimenté
- Essayez de réinitialiser le PICKit 4
- Vérifiez que le bon device est sélectionné

## 📚 Ressources

- [Documentation ATmega328P](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [Microchip Studio](https://www.microchip.com/en-us/tools-resources/develop/microchip-studio)
- [PICKit 4 User Guide](https://ww1.microchip.com/downloads/en/DeviceDoc/50002729B.pdf)
- [ST7789 Datasheet](https://cdn-shop.adafruit.com/product-files/3787/3787_tft_240x135_datasheet.pdf)

## 📝 Notes

- L'ATmega328P a 32KB de flash et 2KB de RAM
- La communication avec l'ESP32 se fait via UART (pas I2C)
- L'écran ST7789 utilise SPI pour la communication
- Le capteur de lumière est lu via ADC toutes les ~100ms
- L'affichage est mis à jour toutes les ~200ms si la luminosité change
- Le watchdog timer est désactivé pour éviter les resets
- Les messages de debug sont envoyés via UART vers l'ESP32
- Le backlight s'active automatiquement si la luminosité est faible (< 200)
