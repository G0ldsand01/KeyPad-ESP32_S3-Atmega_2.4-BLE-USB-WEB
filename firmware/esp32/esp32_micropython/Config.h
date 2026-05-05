/*
 * Config.h — Configuration centralisée du Macropad ESP32-S3
 * Inspiré de MacroPad (aayushchouhan24) — logique modulaire, HID standalone
 *
 * Le device fonctionne sur n'importe quel appareil (phone, PC) sans app.
 * Configurable via interface web (Web Serial / Web Bluetooth).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ─── Version ─────────────────────────────────────────────────────────────────
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

// ─── Matrice de touches 5×4 ─────────────────────────────────────────────────
#define NUM_ROWS 5
#define NUM_COLS 4
#define NUM_KEYS (NUM_ROWS * NUM_COLS)
// Touche réservée au changement de profil (non mappable via Web) — coin haut-gauche 0,0.
#define PROFILE_KEY_ROW 0
#define PROFILE_KEY_COL 0

static const uint8_t ROW_PINS[NUM_ROWS] = {4, 5, 6, 7, 15};   // R0..R4
static const uint8_t COL_PINS[NUM_COLS] = {16, 17, 18, 8};    // C0..C3

#define DEBOUNCE_MS 25
#define REPEAT_DELAY_MS 500
#define REPEAT_INTERVAL_MS 50

// ─── Veille profonde + réveil par touche (toute touche = n'importe quelle colonne) ─
// Avant veille : toutes les colonnes en sortie LOW, lignes en pull-up → une touche
// relie ligne/colonne → ligne à 0 → EXT1 réveil (ESP32-S3).
#define ENABLE_KEYPAD_SLEEP_WAKE     1
// Temps sans activité (touche / encodeur) avant entrée en veille (ms).
#define KEY_IDLE_DEEP_SLEEP_MS       (1u * 60u * 1000u)  // 1 min — ajuster au besoin
// GPIO pour oscilloscope : HIGH dès le début du réveil / boot jusqu'à fin de setup();
// courte impulsion HIGH à l'entrée en veille. Conflit si USE_ESP32_DISPLAY_ST7789 et RST=21.
#define SLEEP_TIMING_GPIO            21
// 0 = ne pas entrer en veille si BLE connecté (recommandé pour garder la liaison).
#define SLEEP_WHEN_BLE_CONNECTED     0
// Réveil EXT1 sans touche stable : 0 = repartir **toujours** en veille (pas de boot fantôme).
// >0 = après ce nombre de réveils « fantômes » d’affilée, boot complet (debug / matrice défaillante).
#define SLEEP_EXT1_PHANTOM_MAX_BOOT_AFTER 0
// 1 = logs détaillés (GPIO EXT1 + phrases à chaque retour veille « pré-boot »). 0 = silencieux si pas encore fini setup().
#define SLEEP_EXT1_VERBOSE_SERIAL       0

// ─── Encodeur rotatif ───────────────────────────────────────────────────────
#define ENC_CLK_PIN 3
#define ENC_DT_PIN 46
#define ENC_SW_PIN 9

#define ENC_VOLUME_COOLDOWN_MS 40   // 1 commande volume par cran, min 40ms entre chaque
#define BLE_VOLUME_STEP_DELAY_MS 130  // Android: espacement min entre rapports Consumer (évite "max ou rien")
#define ENABLE_ENCODER_VOLUME 1    // 1 = activé. Lecture avant scan matrice pour éviter interférences.

// ─── USB Passthrough (obsolète avec hub USB) ───────────────────────────────────
#define ENABLE_USB_PASSTHROUGH 0   // Hub USB = clavier + fingerprint simultanés

// ─── BLE Switch appareil (PROFILE+1 maintenu 2s) ──────────────────────────────
#define ENABLE_BLE_DEVICE_SWITCH 1
#define BLE_SWITCH_COMBO_MS 2000

// ─── UART ATmega ────────────────────────────────────────────────────────────
// Câblage: ESP32 TX(10) -> 2k2 -> ATmega RX(PD0)  |  ATmega TX(PD1) -> diviseur 2k2/3k3 -> ESP32 RX(11)
// IMPORTANT: L'ATmega en 5V envoie 5V sur PD1. L'ESP32 n'est PAS 5V tolerant!
// Diviseur requis: PD1 -> 2k2 -> [jonction vers ESP32 RX] -> 3k3 -> GND  (≈3V)
// GPIO 43/44 = terminal USB (ne pas utiliser pour ATmega)
#define ENABLE_ATMEGA_UART 1
#define ATMEGA_UART_TX 10
#define ATMEGA_UART_RX 11
// Doit correspondre au débit programmé dans firmware/atmega/atmega_light/main.cpp (57600 @ 8 MHz, U2X).
#define ATMEGA_UART_BAUD 57600

#define CMD_READ_LIGHT 0x01
#define CMD_SET_LED 0x02
#define CMD_GET_LED 0x03
#define CMD_UPDATE_DISPLAY 0x04
#define CMD_SET_DISPLAY_DATA 0x05
#define CMD_SET_DISPLAY_IMAGE 0x08
#define CMD_SET_DISPLAY_IMAGE_CHUNK 0x09
#define CMD_SET_ATMEGA_DEBUG 0x0A
#define CMD_SET_ATMEGA_LOG_LEVEL 0x0B
#define CMD_SET_LAST_KEY 0x0C
#define CMD_SET_SCREEN_MODE 0x0D  // ESP32 -> ATmega: 0=UI data, 1=QR image (runtime)
#define CMD_PREPARE_SLEEP 0x0E   // ESP32 -> ATmega: avant veille profonde (BL off + TFT SLPIN)
#define CMD_RESUME_FROM_SLEEP 0x0F // ESP32 -> ATmega: SLPOUT + DISPON + rétro (payload: [bl_on][bl_0..255])
// IMPORTANT: utiliser des valeurs hors ASCII (évite faux positifs dans les logs \r\n)
#define CMD_SHOW_QR 0xF1   // ATmega -> ESP32: demander l'affichage du QR (image)
#define CMD_IMAGE_ACK 0xF2 // ATmega -> ESP32: ACK d'un chunk image (idx low/high)

// ─── LEDs ───────────────────────────────────────────────────────────────────
// Strip NeoPixel / SK6812 (rétroéclairage RVB) — GPIO 48 sur carte finale
#define ENABLE_LED_STRIP 1
#define LED_STRIP_PIN 48
// Buffer NeoPixel max (nombre de pixels alloués). La longueur réelle est réglable via NVS / Web (≤ max).
// Nécessite Adafruit NeoPixel avec updateLength() (versions récentes de la librairie).
// LED_STRIP_DEFAULT_ACTIVE = longueur par défaut au premier boot (ex. 17 = 1 module + 16 touches).
// LED_STRIP_FIRST_PIXEL_RESERVED = 1 : pixel 0 toujours éteint (LED module), touches à partir de l’index 1.
#define LED_STRIP_MAX 48
#define LED_STRIP_DEFAULT_ACTIVE 17
#define LED_STRIP_FIRST_PIXEL_RESERVED 0
// LED blanche PWM séparée (si câblée). -1 = désactivé.
#define LED_PWM_PIN 45
#define ESP32_LIGHT_ADC_PIN 2
// ─── Keymap par défaut (grille physique) ────────────────────────────────────
// [PROFILE] [/] [*] [-]
// [7] [8] [9] [+]
// [4] [5] [6]
// [1] [2] [3] [=]
// [0] [.]
// Défini dans le .ino principal

// ─── Codes HID Keypad (Usage Page 0x07) ──────────────────────────────────────
// BLE: envoi direct. USB: +0x88 pour bypass ASCII (non-printing key).
#define HID_USB_RAW_OFFSET 0x88

// Keypad 0-9, / * - + . =, flèches
#define HID_KP_1 0x59
#define HID_KP_2 0x5A
#define HID_KP_3 0x5B
#define HID_KP_4 0x5C
#define HID_KP_5 0x5D
#define HID_KP_6 0x5E
#define HID_KP_7 0x5F
#define HID_KP_8 0x60
#define HID_KP_9 0x61
#define HID_KP_0 0x62
#define HID_KP_SLASH 0x54
#define HID_KP_ASTERISK 0x55
#define HID_KP_MINUS 0x56
#define HID_KP_PLUS 0x57
#define HID_KP_DOT 0x63
#define HID_KP_EQUALS 0x67
#define HID_KP_LEFT 0x50
#define HID_KP_RIGHT 0x52
#define HID_KP_UP 0x53
#define HID_KP_DOWN 0x51

// Consumer Control (volume, media)
#define CONSUMER_VOL_UP 0xE9
#define CONSUMER_VOL_DOWN 0xEA
#define CONSUMER_MUTE 0xE2
// Keyboard page volume (0x7F, 0x80, 0x81) — Android BLE accepte mieux que Consumer Control
#define HID_KB_VOL_UP   0x80
#define HID_KB_VOL_DOWN 0x81
#define HID_KB_MUTE     0x7F
#define CONSUMER_NEXT 0xB5
#define CONSUMER_PREV 0xB6
#define CONSUMER_PLAY_PAUSE 0xCD

// ─── BLE UUIDs ──────────────────────────────────────────────────────────────
#define BLE_SVC_HID "1812"
#define BLE_CHAR_INPUT "2A4D"
#define BLE_SVC_SERIAL "0000ffe0-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_SERIAL "0000ffe1-0000-1000-8000-00805f9b34fb"

// ─── Display update / rafraîchissement TFT local ESP32 ─────────────────────
#define DISPLAY_UPDATE_INTERVAL_MS 1000

// ─── Écran ST7789 optionnel sur l'ESP32 (HUD ; couleurs type global.css) ────
// 0 = aucun TFT sur l'ESP32. 1 = actif — adapte les pins à ton câblage
// (ne pas utiliser GPIO 10/11 si UART ATmega activé).
#define USE_ESP32_DISPLAY_ST7789 0
#if USE_ESP32_DISPLAY_ST7789
#define ESP32_TFT_SCK            14
#define ESP32_TFT_MOSI           13
#define ESP32_TFT_CS             12
#define ESP32_TFT_DC             47
#define ESP32_TFT_RST            21
#define ESP32_TFT_BL             38
#define ESP32_TFT_BL_INVERT      0
#define ESP32_TFT_SPI_MODE       0
#define ESP32_TFT_CS_GND         0
#define ESP32_TFT_HW_PIN_TEST    0
#define ESP32_TFT_MANUAL_ST7789_TEST 0
#define ESP32_TFT_BOOT_TEST      0
#endif

// ─── Mode écran ATmega (compile-time) ───────────────────────────────────────
// Permet de "hardcoder" quel flux est autorisé vers l'ATmega:
// - UI only  : on envoie seulement les trames UI-data / last_key (pas de QR)
// - QR only  : on envoie seulement l'image QR (pas de UI-data)
// - AUTO     : comportement normal (UI-data), avec option "demo_mode" pour afficher QR.
//
// Valeurs:
// 0 = UI only
// 1 = QR only
// 2 = AUTO
#define ATMEGA_SCREEN_MODE 2
#define ATMEGA_SCREEN_MODE_UI_ONLY 0
#define ATMEGA_SCREEN_MODE_QR_ONLY 1
#define ATMEGA_SCREEN_MODE_AUTO    2

// 1 = à chaque boot / reset: profil actif = DEMO (QR). 0 = dernier profil sauvegardé en NVS.
#define BOOT_START_ON_DEMO_PROFILE 1

// 1 = au boot, si le profil actif est USER1 (pas DEMO), envoyer d'abord une image QR complète sur l'ATmega,
// puis apply_screen_mode repasse en mode data + UI (réinitialise l'écran).
#define ATMEGA_BOOT_QR_SPLASH_FOR_USER1 1

// Écran principal : ATmega (UART). Optionnel : second TFT ST7789 sur ESP32 (voir USE_ESP32_DISPLAY_ST7789).

// ─── Alimentation batterie (BLE) ─────────────────────────────────────────────
// Si BLE n'est pas visible avec batterie 3.7V: la radio BLE consomme ~80-100 mA en TX.
// Vérifier: (1) Tension stable 3.0-3.6V sur ESP32 (LDO si batterie 4.2V max)
//           (2) Courant suffisant (batterie dégradée = chute de tension sous charge)
//           (3) Entrée 5V: utiliser un boost 3.7V→5V, pas de connexion directe batterie→5V

// ─── Light sensor poll ──────────────────────────────────────────────────────
#define LIGHT_POLL_INTERVAL_MS 30000   // USB: mise à jour toutes les 30 s
#define LIGHT_POLL_INTERVAL_BLE_MS 60000  // BLE: toutes les 60 s (pour LED)
#define LIGHT_POLL_MIN_INTERVAL_MS 30000  // Throttle: min 30s entre 2 CMD_READ_LIGHT
#define LIGHT_THRESHOLD 550  // < 500 = sombre (LED ON). Si capteur inversé (haut=sombre): utiliser >= pour ON
#define LIGHT_SENSOR_INVERTED 0  // 0 = ADC >= 500 = clair (LED OFF). ADC < 500 = sombre (LED ON)

// ─── Capteur luminosité sur ESP32 (ADC) ─────────────────────────────────────
// 1 = lire la luminosité directement via une pin ADC ESP32, au lieu de CMD_READ_LIGHT (ATmega).
// La valeur est normalisée sur 0..1023 pour rester compatible avec le reste du code (LIGHT_THRESHOLD).
#define USE_ESP32_LIGHT_SENSOR 1
// Choisis une pin ADC valide pour ton ESP32-S3 (ex: GPIO1/2/14/15 selon ta carte).
// IMPORTANT: évite les pins utilisées par USB, UART, matrice, etc.
#define ESP32_LIGHT_ADC_PIN 2
// Résolution ADC utilisée pour la normalisation. Sur ESP32 Arduino, analogRead retourne souvent 0..4095.
#define ESP32_LIGHT_ADC_MAX 4095

#endif // CONFIG_H
