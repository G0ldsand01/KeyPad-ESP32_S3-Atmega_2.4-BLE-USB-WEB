/*
 * Configuration et définitions pour ESP32-S3 Macropad
 */

#ifndef CONFIG_H
#define CONFIG_H

// ==================== CONFIGURATION HARDWARE ====================

// UART Configuration pour ATmega328P
// Câblage : TX de l'ESP32 → RX de l'ATmega (PD0, Pin 2), RX de l'ESP32 → TX de l'ATmega (PD1, Pin 3)
#define ATMEGA_UART_TX 10   // GPIO10 (TX de l'ESP32 → RX de l'ATmega)
#define ATMEGA_UART_RX 11   // GPIO11 (RX de l'ESP32 → TX de l'ATmega)
#define ATMEGA_UART_BAUD 115200  // 115200 bauds

// UART Configuration pour Fingerprint Sensor
// Câblage : TX de l'ESP32 → RX du fingerprint, RX de l'ESP32 → TX du fingerprint
// NOTE: Utilise Serial2 (UART hardware) - pins différents de la matrice de touches
#define FINGERPRINT_UART_TX 43   // GPIO43 (TX de l'ESP32 → RX du fingerprint) - Serial2 TX
#define FINGERPRINT_UART_RX 44   // GPIO44 (RX de l'ESP32 → TX du fingerprint) - Serial2 RX
#define FINGERPRINT_UART_BAUD 57600  // 57600 bauds (standard pour capteurs d'empreinte)

// Commandes UART pour ATmega (protocole: [CMD] [DATA...] [CRC] [EOL])
#define CMD_READ_LIGHT 0x01  // Lire la luminosité
#define CMD_SET_LED 0x02     // Définir la luminosité LED (0-255)
#define CMD_GET_LED 0x03     // Obtenir la luminosité LED actuelle
#define CMD_UPDATE_DISPLAY 0x04  // Mettre à jour l'affichage ST7789
#define CMD_SET_DISPLAY_DATA 0x05  // Envoyer les données d'affichage (profil, mode, etc.)
#define CMD_SET_DISPLAY_BRIGHTNESS 0x06  // Définir la luminosité de l'écran (0-255)
#define CMD_SET_DISPLAY_MODE 0x07  // Définir le mode d'affichage (data/image/gif)
#define CMD_SET_DISPLAY_IMAGE 0x08  // Envoyer une image RGB565 (transmission par chunks)
#define CMD_SET_DISPLAY_IMAGE_CHUNK 0x09  // Envoyer un chunk d'image (suivi de chunk_index, chunk_size, data)
#define CMD_SET_ATMEGA_DEBUG 0x0A  // Activer/désactiver le debug UART sur l'ATmega
#define CMD_SET_ATMEGA_LOG_LEVEL 0x0B  // Définir le niveau de log de l'ATmega

// Capteur d'empreinte
//#define FINGERPRINT_RX 16
//#define FINGERPRINT_TX 17
//#define FINGERPRINT_BAUDRATE 57600

// Matrice de touches - Gestion directe par ESP32
#define MATRIX_COL0 16   // GPIO 16 = Colonne 0
#define MATRIX_COL1 17   // GPIO 17 = Colonne 1
#define MATRIX_COL2 18   // GPIO 18 = Colonne 2
#define MATRIX_COL3 8    // GPIO 8 = Colonne 3

#define MATRIX_ROW0 4   // GPIO 4 = Ligne 0
#define MATRIX_ROW1 5   // GPIO 5 = Ligne 1
#define MATRIX_ROW2 6   // GPIO 6 = Ligne 2
#define MATRIX_ROW3 7   // GPIO 7 = Ligne 3
#define MATRIX_ROW4 15   // GPIO 15 = Ligne 4

// Encodeur rotatif pour volume
// IMPORTANT: Ne pas utiliser GPIO 19/20 (USB_D-/USB_D+) pour l'encodeur !
// Ces pins sont réservés pour le port USB natif
#define ENCODER_CLK 3  // GPIO 3 pour CLK
#define ENCODER_DT 46   // GPIO 46 pour DT
#define ENCODER_SW 9   // GPIO 9 pour bouton (SW)



// Matrice de touches - Version complète 4x5
#define MATRIX_ROWS 5
#define MATRIX_COLS 4
#define TOTAL_KEYS 20

// ==================== LIMITES DE CONFIGURATION ====================

#define MAX_PROFILES 10
#define MAX_KEYS_PER_PROFILE 20
#define MAX_MACRO_STEPS 10
#define MAX_MODIFIERS 4

// ==================== NOMS DE PRÉFÉRENCES ====================

#define PREF_NAMESPACE "macropad"
#define PREF_PROFILE_COUNT "profileCount"
#define PREF_ACTIVE_PROFILE "activeProfile"
#define PREF_PROFILE_PREFIX "profile"
#define PREF_BACKLIGHT_ENABLED "backlightOn"
#define PREF_BACKLIGHT_BRIGHTNESS "backlightBr"
#define PREF_AUTO_BRIGHTNESS "autoBright"
#define PREF_ENV_BRIGHTNESS "envBright"  // Mode "selon l'environnement"
#define PREF_FINGERPRINT_ENABLED "fingerOn"
#define PREF_OUTPUT_MODE "outputMode"
#define PREF_DISPLAY_BRIGHTNESS "displayBr"
#define PREF_DISPLAY_MODE "displayMode"

// ==================== CONFIGURATION DEBUG ====================

// Activer le débogage Serial (décommenter pour activer)
// IMPORTANT: Pour téléverser le code, désactivez DEBUG_ENABLED ou utilisez Serial1/Serial2
// Le mode Serial (USB CDC) entre en conflit avec USB HID et empêche le téléversement
// DÉSACTIVÉ TEMPORAIREMENT POUR PERMETTRE LE TÉLÉVERSEMENT
// Décommentez la ligne suivante pour activer le débogage (nécessite Serial1 ou Serial2)
// #define DEBUG_ENABLED
#define DEBUG_SERIAL_BAUD 115200
// Note: Sur ESP32-S3, Serial utilise le port USB natif
// Pour éviter les conflits avec USB HID, vous pouvez utiliser Serial1 ou Serial2 (UART hardware)
// Décommentez l'une des options ci-dessous pour utiliser un UART hardware :
// #define DEBUG_USE_SERIAL1  // Utiliser Serial1 (UART hardware) - nécessite connexion externe
// #define DEBUG_USE_SERIAL2  // Utiliser Serial2 (UART hardware) - nécessite connexion externe
// Si aucune option n'est définie, Serial (USB natif) sera utilisé

// Niveaux de débogage (décommenter ceux que vous voulez activer)
#define DEBUG_UART         // Messages UART (communication ATmega)
#define DEBUG_HID          // Messages HID (touches pressées)
#define DEBUG_WEB_UI       // Messages Web UI (JSON reçus/envoyés)
#define DEBUG_BACKLIGHT    // Messages backlight (changements de luminosité)
#define DEBUG_LIGHT_SENSOR // Messages capteur de lumière
#define DEBUG_FINGERPRINT  // Messages fingerprint sensor

#endif
