# Fonctionnalités du Macropad

Description détaillée des fonctions de l'interface web et du firmware.

> Voir le [README principal](../README.md) pour la vue d'ensemble du projet.

## Interface web

### Onglet Principal

#### Pavé numérique (NumpadGrid)

- **Grille 5×4** : 20 touches configurables
- **Touche PROFILE (0-0)** : Change de profil (Profil 1, Configuration, etc.)
- **Sélection** : Cliquer sur une touche pour la configurer
- **Feedback visuel** : Touches configurées en vert, touche sélectionnée en bleu

#### Configuration des touches (ConfigPanel)

| Type | Description |
|------|-------------|
| **Touche simple** | Une touche (a, ENTER, SPACE, etc.) |
| **Modificateur** | CTRL, SHIFT, ALT, GUI (Windows/Cmd) |
| **Média** | Volume +/-, Mute, Play/Pause, Suivant, Précédent |
| **Macro** | Séquence de touches avec délai (ex: CTRL+C, CTRL+V, ENTER) |

**Presets média** : Volume +, Volume -, Mute, Play/Pause, Suivant, Précédent, Stop, Autre

#### Profils

- Création et suppression de profils
- Profil "Configuration" : touches de navigation partagées
- Stockage persistant dans la flash ESP32 (transfert entre appareils)
- Sauvegarde automatique à chaque modification

### Onglet Rétro-éclairage

- **Luminosité** : Slider 0–100 %
- **Selon l'environnement** : Ajustement automatique via capteur de lumière (ATmega)
- **État** : Affichage du niveau de lumière détecté

### Onglet Paramètres

#### Moniteur série (USB)

- Affichage de la sortie Serial de l'ESP32
- Défilement auto, max lignes (100–1000)
- Bouton Effacer

#### Connexion

- **Nom BLE** : Nom de l'appareil Bluetooth
- **Thème** : Sombre, Clair, Système

#### Debug & Logging

- Debug ESP32, Logging ESP32, Logging ATmega
- Niveaux de log : Aucun, Erreurs, Avertissements, Info, Debug, Verbose

#### Options de debug

- HID, I2C, Web UI, Display, Config

#### Mise à jour OTA

- Flasher le firmware sans fil via BLE ou USB
- Fichier `.bin` exporté depuis Arduino IDE
- Barre de progression, redémarrage automatique

#### Données

- **Exporter la configuration** : Télécharge un fichier JSON
- **Importer la configuration** : Charge un JSON exporté
- **Réinitialiser les paramètres** : Remet les paramètres par défaut

#### Appliquer

- Bouton "Appliquer les paramètres" pour envoyer la config à l'ESP32

## Firmware ESP32

### Matrice de touches

- **Pins** : Rows 4,5,6,7,15 ; Cols 16,17,18,8
- **Debounce** : 25 ms
- **Répétition** : 500 ms délai, 50 ms intervalle

### Encodeur rotatif

- **Volume** : Rotation = Volume +/- (Consumer Control ou HID)
- **Mute** : Bouton = Mute
- **Pins** : CLK=3, DT=46, SW=9

### BLE

- **Switch appareil** : PROFILE + 1 maintenu 2 s → déconnecte et permet de connecter un autre appareil
- **UUID** : Service 0xFFE0, Caractéristique 0xFFE1

### Communication ATmega (UART)

- **TX** : GPIO 10 → ATmega RX
- **RX** : GPIO 11 ← ATmega TX (diviseur 2k2/3k3 pour 5V→3.3V)
- **Baud** : 9600

### Commandes ATmega

| Cmd | Description |
|-----|-------------|
| 0x01 | Lire capteur de lumière |
| 0x02 | Définir LED |
| 0x03 | Obtenir état LED |
| 0x04 | Mettre à jour l'écran |
| 0x05 | Données écran |
| 0x08 | Image écran |
| 0x09 | Chunk image |
| 0x0A | Debug ATmega |
| 0x0B | Niveau log ATmega |
| 0x0C | Dernière touche pressée |

## Firmware ATmega

- **Matrice** : Scan 4×5 via I2C ou direct
- **I2C** : Adresse 0x08, 100 kHz
- **Capteur lumière** : ADC pour rétro-éclairage auto
- **Écran** : ST7789 ou compatible, données via UART

## Protocole JSON (Web ↔ ESP32)

### Messages envoyés par l'interface

- `config` : Configuration complète (profils, keys)
- `backlight` : Rétro-éclairage
- `display_*` : Écran
- `ota_start`, `ota_chunk`, `ota_end` : Mise à jour OTA
- `get_light` : Demande niveau lumière
- `set_device_name` : Nom BLE

### Messages reçus de l'ESP32

- `status` : État de connexion
- `config` : Config actuelle
- `keypress` : Touche pressée
- `ota_status` : Progression OTA
- `uart_log` : Log série ATmega
- `light` : Niveau lumière
