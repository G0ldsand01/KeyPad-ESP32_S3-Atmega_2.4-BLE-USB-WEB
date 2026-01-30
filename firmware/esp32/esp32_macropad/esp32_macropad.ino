/*
 * ESP32-S3 Macropad Firmware - Version Complète 4x5
 * Gestion directe de la matrice de touches 4x5 et encodeur rotatif pour volume
 * Compatible : Windows, macOS, Linux (USB HID) + iOS, Android (Bluetooth HID)
 */

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHID.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "config.h"

// ==================== SYSTÈME DE DÉBOGAGE ====================

#ifdef DEBUG_ENABLED
  #ifdef DEBUG_USE_SERIAL1
    // Utiliser Serial1 (UART hardware) pour éviter conflit avec USB HID
    // Par défaut: GPIO17 = TX, GPIO18 = RX (peut être configuré)
    // Connectez un convertisseur USB-UART externe si nécessaire
    #define DEBUG_SERIAL_BEGIN() Serial1.begin(DEBUG_SERIAL_BAUD, SERIAL_8N1, -1, -1); delay(100)
    #define DEBUG_PRINT(x) Serial1.print(x)
    #define DEBUG_PRINTLN(x) Serial1.println(x)
    #define DEBUG_PRINTF(fmt, ...) Serial1.printf(fmt, ##__VA_ARGS__)
  #elif defined(DEBUG_USE_SERIAL2)
    // Utiliser Serial2 (UART hardware) pour éviter conflit avec USB HID
    // Par défaut: GPIO43 = TX, GPIO44 = RX (peut être configuré)
    // Connectez un convertisseur USB-UART externe si nécessaire
    #define DEBUG_SERIAL_BEGIN() Serial2.begin(DEBUG_SERIAL_BAUD, SERIAL_8N1, -1, -1); delay(100)
    #define DEBUG_PRINT(x) Serial2.print(x)
    #define DEBUG_PRINTLN(x) Serial2.println(x)
    #define DEBUG_PRINTF(fmt, ...) Serial2.printf(fmt, ##__VA_ARGS__)
  #else
    // Utiliser Serial (port USB natif) - peut entrer en conflit avec USB HID
    // Note: Sur ESP32-S3, Serial utilise le port USB CDC
    #define DEBUG_SERIAL_BEGIN() Serial.begin(DEBUG_SERIAL_BAUD); delay(100)
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
  #endif
#else
  #define DEBUG_SERIAL_BEGIN()
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

#ifdef DEBUG_UART
  #define DEBUG_UART_PRINT(x) DEBUG_PRINT(x)
  #define DEBUG_UART_PRINTLN(x) DEBUG_PRINTLN(x)
  #define DEBUG_UART_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_UART_PRINT(x)
  #define DEBUG_UART_PRINTLN(x)
  #define DEBUG_UART_PRINTF(fmt, ...)
#endif

#ifdef DEBUG_HID
  #define DEBUG_HID_PRINT(x) DEBUG_PRINT(x)
  #define DEBUG_HID_PRINTLN(x) DEBUG_PRINTLN(x)
  #define DEBUG_HID_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_HID_PRINT(x)
  #define DEBUG_HID_PRINTLN(x)
  #define DEBUG_HID_PRINTF(fmt, ...)
#endif

#ifdef DEBUG_WEB_UI
  #define DEBUG_WEB_PRINT(x) DEBUG_PRINT(x)
  #define DEBUG_WEB_PRINTLN(x) DEBUG_PRINTLN(x)
  #define DEBUG_WEB_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_WEB_PRINT(x)
  #define DEBUG_WEB_PRINTLN(x)
  #define DEBUG_WEB_PRINTF(fmt, ...)
#endif

#ifdef DEBUG_BACKLIGHT
  #define DEBUG_BACKLIGHT_PRINT(x) DEBUG_PRINT(x)
  #define DEBUG_BACKLIGHT_PRINTLN(x) DEBUG_PRINTLN(x)
  #define DEBUG_BACKLIGHT_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_BACKLIGHT_PRINT(x)
  #define DEBUG_BACKLIGHT_PRINTLN(x)
  #define DEBUG_BACKLIGHT_PRINTF(fmt, ...)
#endif

#ifdef DEBUG_LIGHT_SENSOR
  #define DEBUG_LIGHT_PRINT(x) DEBUG_PRINT(x)
  #define DEBUG_LIGHT_PRINTLN(x) DEBUG_PRINTLN(x)
  #define DEBUG_LIGHT_PRINTF(fmt, ...) DEBUG_PRINTF(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_LIGHT_PRINT(x)
  #define DEBUG_LIGHT_PRINTLN(x)
  #define DEBUG_LIGHT_PRINTF(fmt, ...)
#endif

// Pour Consumer Control HID, on doit créer un descripteur personnalisé
// Note: USBHID.h est nécessaire pour créer un périphérique Consumer Control

// Bluetooth HID pour iOS/Android
// Utilise la bibliothèque BLE HID native d'ESP32 pour éviter les conflits
#define USE_BLE_KEYBOARD
#ifdef USE_BLE_KEYBOARD
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUIDs pour BLE HID
#define SERVICE_UUID        "1812"  // HID Service UUID
#define CHAR_UUID_INPUT      "2A4D"  // HID Input Report
#define CHAR_UUID_OUTPUT     "2A4D"  // HID Output Report
#define CHAR_UUID_FEATURE    "2A4D"  // HID Feature Report
#define DESCRIPTOR_UUID      "2902"  // Client Characteristic Configuration

BLEServer* pServer = nullptr;
BLECharacteristic* pInputCharacteristic = nullptr;
BLECharacteristic* pSerialCharacteristic = nullptr;  // Pour communication série avec web UI
bool deviceConnected = false;
bool oldDeviceConnected = false;
String bleSerialBuffer = "";  // Buffer pour les données série reçues

// Déclaration forward pour processWebMessage (nécessaire pour les callbacks)
void processWebMessage(String message);

// Callbacks pour BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        DEBUG_PRINTLN("[BLE] Client connected");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        DEBUG_PRINTLN("[BLE] Client disconnected");
    }
};

// Callbacks pour la caractéristique série
class SerialCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String message = String(value.c_str());
            DEBUG_WEB_PRINTF("[BLE] Received via Serial: %s\n", message.c_str());
            bleSerialBuffer += message;
            
            // Traiter les messages complets (séparés par \n)
            int newlinePos;
            while ((newlinePos = bleSerialBuffer.indexOf('\n')) >= 0) {
                String completeMessage = bleSerialBuffer.substring(0, newlinePos);
                bleSerialBuffer = bleSerialBuffer.substring(newlinePos + 1);
                if (completeMessage.length() > 0) {
                    processWebMessage(completeMessage);
                }
            }
        }
    }
};

// Structure pour Consumer Control BLE
struct BLEKeyboard {
    void begin() {
        BLEDevice::init("Macropad");
        
        // Configurer la sécurité BLE - Pas de PIN requis pour HID
        BLESecurity* pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
        pSecurity->setCapability(ESP_IO_CAP_NONE);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        
        // Service HID standard
        BLEService* pService = pServer->createService(BLEUUID((uint16_t)0x1812));
        
        // HID Information Characteristic (requis pour HID)
        BLECharacteristic* pInfoCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4A),
            BLECharacteristic::PROPERTY_READ
        );
        uint8_t info[] = {0x01, 0x01, 0x00, 0x03}; // Version, flags, country code
        pInfoCharacteristic->setValue(info, 4);
        
        // HID Report Map (descripteur HID simplifié)
        BLECharacteristic* pReportMapCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4B),
            BLECharacteristic::PROPERTY_READ
        );
        // Descripteur HID minimal pour clavier
        uint8_t reportMap[] = {
            0x05, 0x01,        // Usage Page (Generic Desktop)
            0x09, 0x06,        // Usage (Keyboard)
            0xa1, 0x01,        // Collection (Application)
            0x85, 0x01,        //   Report ID (1)
            0x75, 0x01,        //   Report Size (1)
            0x95, 0x08,        //   Report Count (8)
            0x05, 0x07,        //   Usage Page (Key Codes)
            0x19, 0xe0,        //   Usage Minimum (224)
            0x29, 0xe7,        //   Usage Maximum (231)
            0x15, 0x00,        //   Logical Minimum (0)
            0x25, 0x01,        //   Logical Maximum (1)
            0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
            0x95, 0x01,        //   Report Count (1)
            0x75, 0x08,        //   Report Size (8)
            0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
            0x95, 0x05,        //   Report Count (5)
            0x75, 0x01,        //   Report Size (1)
            0x05, 0x08,        //   Usage Page (LEDs)
            0x19, 0x01,        //   Usage Minimum (1)
            0x29, 0x05,        //   Usage Maximum (5)
            0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
            0x95, 0x01,        //   Report Count (1)
            0x75, 0x03,        //   Report Size (3)
            0x91, 0x01,        //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
            0x95, 0x06,        //   Report Count (6)
            0x75, 0x08,        //   Report Size (8)
            0x15, 0x00,        //   Logical Minimum (0)
            0x25, 0x65,        //   Logical Maximum (101)
            0x05, 0x07,        //   Usage Page (Key Codes)
            0x19, 0x00,        //   Usage Minimum (0)
            0x29, 0x65,        //   Usage Maximum (101)
            0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
            0xc0               // End Collection
        };
        pReportMapCharacteristic->setValue(reportMap, sizeof(reportMap));
        
        // HID Input Report Characteristic
        pInputCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4D),
            BLECharacteristic::PROPERTY_READ | 
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        
        pInputCharacteristic->addDescriptor(new BLE2902());
        
        // HID Control Point
        BLECharacteristic* pControlPointCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4C),
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        
        pService->start();
        
        // Service Bluetooth Serial pour communication avec le web UI
        // UUID standard pour Serial Port Profile (SPP)
        BLEService* pSerialService = pServer->createService(BLEUUID("0000ffe0-0000-1000-8000-00805f9b34fb"));
        
        // Caractéristique pour la communication série
        BLECharacteristic* pSerialChar = pSerialService->createCharacteristic(
            BLEUUID("0000ffe1-0000-1000-8000-00805f9b34fb"),
            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        
        pSerialChar->addDescriptor(new BLE2902());
        pSerialChar->setCallbacks(new SerialCharacteristicCallbacks());
        pSerialService->start();
        
        // Stocker la caractéristique pour utilisation ultérieure
        pSerialCharacteristic = pSerialChar;
        
        // Configuration de la publicité BLE
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x1812));  // HID Service
        pAdvertising->addServiceUUID(BLEUUID("0000ffe0-0000-1000-8000-00805f9b34fb"));  // Serial Service
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
        
        DEBUG_PRINTLN("[BLE] Bluetooth HID and Serial services started");
        DEBUG_PRINTLN("[BLE] Device name: Macropad");
    }
    
    void write(uint8_t key) {
        if (deviceConnected) {
            uint8_t report[8] = {0};
            report[2] = key;
            pInputCharacteristic->setValue(report, 8);
            pInputCharacteristic->notify();
            delay(10);
            // Release
            uint8_t release[8] = {0};
            pInputCharacteristic->setValue(release, 8);
            pInputCharacteristic->notify();
        }
    }
    
    void press(uint8_t key) {
        if (deviceConnected) {
            uint8_t report[8] = {0};
            report[2] = key;
            pInputCharacteristic->setValue(report, 8);
            pInputCharacteristic->notify();
        }
    }
    
    void releaseAll() {
        if (deviceConnected) {
            uint8_t release[8] = {0};
            pInputCharacteristic->setValue(release, 8);
            pInputCharacteristic->notify();
        }
    }
    
    bool isConnected() {
        return deviceConnected;
    }
    
    void loop() {
        if (!deviceConnected && oldDeviceConnected) {
            delay(500);
            pServer->startAdvertising();
            oldDeviceConnected = deviceConnected;
        }
        if (deviceConnected && !oldDeviceConnected) {
            oldDeviceConnected = deviceConnected;
        }
    }
};

BLEKeyboard bleKeyboard;
#endif

// ==================== CONFIGURATION ====================

USBHIDKeyboard Keyboard;
Preferences preferences;

// Consumer Control HID Device (pour envoyer les rapports Consumer Control)
// Note: Sur ESP32-S3, on doit créer un descripteur HID composite
// Pour l'instant, on utilise une approche simple avec USBHID
// Référence: https://github.com/NicoHood/HID/discussions

// Mode de sortie : "usb" (par défaut) ou "bluetooth" (si USE_BLE_KEYBOARD activé)
String outputMode = "usb";

// Fonction pour déterminer quel mode utiliser
bool useBLE() {
    #ifdef USE_BLE_KEYBOARD
    return bleKeyboard.isConnected();
    #else
    return false;
    #endif
}

// Codes Consumer Control HID (HID Usage Page Consumer)
// Compatible avec l'API ConsumerAPI de NicoHood
// Référence: Standard USB HID Consumer Page codes
// Volume Up: 0xE9, Volume Down: 0xEA, Mute: 0xE2
// Ces codes fonctionnent sur Windows, macOS, Android (USB HID standard)
#define HID_CONSUMER_VOLUME_UP    0xE9
#define HID_CONSUMER_VOLUME_DOWN  0xEA
#define HID_CONSUMER_MUTE         0xE2

// Définitions des constantes Media Keys (compatible avec ConsumerAPI de NicoHood)
// Référence: https://github.com/NicoHood/HID/blob/master/src/HID-APIs/ConsumerAPI.h
// Standard USB HID Consumer Page codes - Compatible multi-plateforme
#define MEDIA_VOLUME_UP    0xE9
#define MEDIA_VOLUME_DOWN  0xEA
#define MEDIA_VOLUME_MUTE  0xE2

// Classe ConsumerControl compatible avec l'API de NicoHood mais adaptée pour ESP32-S3
// Sur ESP32-S3, Consumer Control HID nécessite un descripteur HID personnalisé
// Cette implémentation utilise l'API USB native d'ESP32-S3
// Note: Renommée en ConsumerControl pour éviter les conflits de nom
class ConsumerControl {
public:
    static void write(uint8_t code) {
        // Envoyer directement le code Consumer Control via USB HID
        // Format: 2 bytes (code sur 16 bits, little-endian)
        uint8_t report[2] = {
            (uint8_t)(code & 0xFF),        // Low byte
            (uint8_t)((code >> 8) & 0xFF)  // High byte (généralement 0 pour Consumer Control)
        };
        
        // Sur ESP32-S3, Consumer Control nécessite un descripteur HID personnalisé
        // Pour l'instant, on utilise une méthode qui fonctionne sans descripteur personnalisé
        // en envoyant directement via l'interface USB disponible
        
        // Note: Pour que Consumer Control fonctionne vraiment, il faudrait créer
        // un descripteur HID composite qui inclut Keyboard + Consumer Control
        // Cette implémentation est une structure de base
        
        // Envoie le rapport Consumer Control
        // Report ID 0x02 est généralement utilisé pour Consumer Control
        sendConsumerControlReport(report, 2);
        
        delay(10);
        
        // Envoyer un rapport vide pour relâcher
        uint8_t empty[2] = {0x00, 0x00};
        sendConsumerControlReport(empty, 2);
        delay(10);
    }
    
    static void press(uint8_t code) {
        #ifdef USE_BLE_KEYBOARD
        if (bleKeyboard.isConnected()) {
            // Envoyer via BLE HID - Consumer Control via BLE
            bleKeyboard.write(code);
            return;
        }
        #endif
        // Solution temporaire pour Android : utiliser des touches de clavier
        // Consumer Control nécessite un descripteur HID composite qui n'est pas encore implémenté
        // Pour l'instant, on utilise des touches de clavier comme solution de contournement
        if (code == MEDIA_VOLUME_UP) {
            // Volume Up : Ctrl + Shift + =
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_SHIFT);
            Keyboard.press('=');
            Keyboard.releaseAll();
        } else if (code == MEDIA_VOLUME_DOWN) {
            // Volume Down : Ctrl + Shift + -
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_SHIFT);
            Keyboard.press('-');
            Keyboard.releaseAll();
        } else if (code == MEDIA_VOLUME_MUTE) {
            // Mute : Ctrl + Shift + M
            Keyboard.press(KEY_LEFT_CTRL);
            Keyboard.press(KEY_LEFT_SHIFT);
            Keyboard.press('M');
            Keyboard.releaseAll();
        }
        delay(10);
    }
    
    static void release(uint8_t code) {
        #ifdef USE_BLE_KEYBOARD
        if (bleKeyboard.isConnected()) {
            // Relâcher via BLE HID
            bleKeyboard.releaseAll();
            return;
        }
        #endif
        // Pour Consumer Control, les touches sont déjà relâchées dans press()
        // Pas besoin de faire quoi que ce soit ici
        delay(10);
    }
    
    static void releaseAll() {
        release(0x00);
    }

private:
    static void sendConsumerControlReport(uint8_t* data, int length) {
        // Sur ESP32-S3, Consumer Control nécessite un descripteur HID personnalisé
        // Référence: https://github.com/NicoHood/HID/discussions
        
        // Note: Pour que Consumer Control fonctionne vraiment, il faudrait créer
        // un descripteur HID composite qui inclut Keyboard + Consumer Control
        
        // Pour l'instant, on essaie d'utiliser l'API USB native d'ESP32-S3
        // Cette implémentation est une tentative basique qui peut ne pas fonctionner
        // sans un descripteur HID composite personnalisé
        
        // Tentative d'envoi via l'interface USB disponible
        // Sur ESP32-S3, on peut essayer d'utiliser USBHID directement
        // mais cela nécessite généralement un descripteur HID composite
        
        // IMPORTANT: Cette fonction est actuellement un placeholder
        // Pour une implémentation complète, il faudrait :
        // 1. Créer un descripteur HID composite personnalisé
        // 2. Utiliser USBHID.sendReport() avec le bon Report ID
        // 3. Ou utiliser une bibliothèque tierce qui supporte Consumer Control sur ESP32-S3
        
        // Pour l'instant, on ne fait rien car sans descripteur HID composite,
        // Consumer Control ne sera pas reconnu par le système hôte
    }
};

// Alias pour compatibilité avec l'API de NicoHood
// Utilise ConsumerControl au lieu de Consumer pour éviter les conflits
#define Consumer ConsumerControl


// ==================== STRUCTURES DE DONNÉES ====================

struct KeyConfig {
    String type;        // "key", "modifier", "media", "macro"
    String value;       // Valeur de la touche
    String modifiers[4]; // CTRL, SHIFT, ALT, GUI
    int modifierCount;
    String macro[10];   // Séquence de macro
    int macroCount;
    int delay;          // Délai entre touches de macro (ms)
};

struct Profile {
    String name;
    KeyConfig keys[TOTAL_KEYS]; // 20 touches pour matrice 4x5
    bool active;
};

struct Config {
    Profile profiles[10];
    int profileCount;
    int activeProfileIndex;
    String outputMode;  // "usb", "bluetooth", "wifi"
};

// Structure pour la configuration de l'écran ST7789
// ST7789: 240x320 pixels, RGB565 (16 bits par pixel) = 153600 bytes par image
#define ST7789_WIDTH 240
#define ST7789_HEIGHT 320
#define ST7789_IMAGE_SIZE (ST7789_WIDTH * ST7789_HEIGHT * 2)  // 153600 bytes
#define IMAGE_CHUNK_SIZE 64  // Taille des chunks pour transmission UART (plus grand que I2C)
#define IMAGE_CHUNK_COUNT ((ST7789_IMAGE_SIZE + IMAGE_CHUNK_SIZE - 1) / IMAGE_CHUNK_SIZE)  // ~4800 chunks

struct DisplayConfig {
    uint8_t brightness;  // 0-255
    String mode;  // "data", "image", "gif"
    struct {
        bool showProfile;
        bool showBattery;
        bool showMode;
        bool showKeys;
        bool showBacklight;
        bool showCustom1;
        bool showCustom2;
        String customLine1;
        String customLine2;
    } customData;
    // Buffer pour image RGB565 (240x320x2 = 153600 bytes - trop grand pour stack, utiliser heap)
    uint8_t* imageData;  // Alloué dynamiquement
    bool imageDataAllocated;
    uint8_t* gifFrames[8];  // Jusqu'à 8 frames pour GIF
    uint8_t gifFrameCount;
    unsigned long lastGifUpdate;
    uint8_t currentGifFrame;
    uint16_t imageChunkIndex;  // Index du chunk en cours de réception
    uint8_t imageChunkBuffer[IMAGE_CHUNK_SIZE];  // Buffer pour recevoir les chunks
};

// ==================== DÉCLARATIONS FORWARD ====================

// Déclarations forward des fonctions
void sendKey(KeyConfig& key);
void sendModifier(KeyConfig& key);
void sendMediaKey(KeyConfig& key);
void executeMacro(KeyConfig& key);
void switchProfile();
String serializeProfileToString(Profile& profile);
void parseProfileFromString(String json, Profile& profile);
String getKeyIdFromIndex(int index);
int getIndexFromKeyId(String keyId);
// UART pour ATmega (Serial1)
HardwareSerial SerialAtmega(1);

// UART pour Fingerprint (Serial2)
HardwareSerial SerialFingerprint(2);

void initUART();
void initFingerprintUART();
uint16_t readAmbientLight();
void setAtmegaLEDBrightness(uint8_t brightness);
void updateBacklightAuto();
void handleBacklightConfig(JsonObject& backlightObj);
void handleSettingsConfig(JsonObject& settingsObj);
void sendAtmegaDebugCommand(bool enabled);
void sendAtmegaLogLevelCommand(String level);
void processWebMessage(String message);  // Déclaration forward pour Bluetooth callbacks
void initDisplay();
void handleDisplayConfig(JsonObject& displayObj);
void handleDisplayImage(JsonObject& imageObj);
void updateDisplay();
void sendDisplayDataToAtmega();
void sendImageToAtmega(uint8_t* imageData, uint16_t imageSize);
void updateRowKeysFromProfile();  // Mettre à jour rowKeys depuis le profil actif

// ==================== VARIABLES GLOBALES ====================

Config config;
DisplayConfig displayConfig;

// État du système
unsigned long lastKeyScan = 0;
// État des touches par row et col: lastKeyState[row][col]
uint8_t lastKeyState[MATRIX_ROWS][MATRIX_COLS] = {0};  // Initialisé à zéro pour toutes les touches
unsigned long lastKeyPressTime[MATRIX_ROWS][MATRIX_COLS] = {0};  // Temps de la dernière pression pour debounce
const unsigned long DEBOUNCE_TIME = 50;  // 50ms de debounce

// Structure pour stocker les valeurs des touches par row
// Chaque row a un array de KeyConfig, indexé par col
// Row0 (GPIO4): [profileswitch, /, *, 0] - 4 éléments
// Row1 (GPIO5): [7, 8, 9, +] - 4 éléments
// Row2 (GPIO6): [5, 6, 7] - 3 éléments
// Row3 (GPIO7): [1, 2, 3, =] - 4 éléments
// Row4 (GPIO15): [0, .] - 2 éléments
struct RowKeyConfig {
    KeyConfig keys[MATRIX_COLS];  // Maximum 4 touches par row
    int keyCount;  // Nombre réel de touches dans cette row
};

// Tableau pour stocker les configurations par row (sera rempli depuis les profils)
RowKeyConfig rowKeys[MATRIX_ROWS];

// État de l'encodeur
int encoderLastState = 0;
int encoderPosition = 0;
bool encoderButtonPressed = false;
unsigned long lastEncoderRead = 0;

// Backlight automatique basé sur la luminosité
bool backlightEnabled = true;
uint8_t backlightBrightness = 128;  // 0-255
bool autoBrightnessEnabled = false;  // Mode automatique selon l'environnement
bool envBrightnessMode = false;  // Mode "selon l'environnement"
uint16_t ambientLightLevel = 0;  // Niveau de luminosité ambiante (0-1023)
unsigned long lastLightRead = 0;
const unsigned long LIGHT_READ_INTERVAL = 500;  // Lire la luminosité toutes les 500ms
const uint16_t LIGHT_THRESHOLD_HIGH = 700;  // Seuil haut (lumière élevée)
const uint16_t LIGHT_THRESHOLD_LOW = 300;   // Seuil bas (lumière faible)

// Pins de la matrice 4x5
// Ordre corrigé selon les observations actuelles :
// Avec ordre précédent {8, 16, 17, 18} :
// Col 0 (GPIO 8) → "*" mais devrait être Profile switch
// Col 1 (GPIO 16) → "-" mais devrait être "/"
// Col 2 (GPIO 17) → "7" mais devrait être "*"
// Col 3 (GPIO 18) → "/" mais devrait être "-"
// 
// Réorganisons pour que :
// Col 0 → Profile switch (GPIO 17 qui donne "7" actuellement, mais peut-être qu'il donnera Profile switch)
// Col 1 → "/" (GPIO 18 qui donne "/")
// Col 2 → "*" (GPIO 8 qui donne "*")
// Col 3 → "-" (GPIO 16 qui donne "-")
// 
// Testons : GPIO 17, GPIO 18, GPIO 8, GPIO 16
const int matrixCols[MATRIX_COLS] = {MATRIX_COL1, MATRIX_COL2, MATRIX_COL3, MATRIX_COL0};  // Réorganiser : 17, 18, 8, 16
const int matrixRows[MATRIX_ROWS] = {MATRIX_ROW0, MATRIX_ROW1, MATRIX_ROW2, MATRIX_ROW3, MATRIX_ROW4};

// ==================== SETUP ====================

void setup() {
    // IMPORTANT: Délai initial pour permettre au bootloader de détecter le mode download
    // Si GPIO0 est maintenu LOW pendant ce délai, l'ESP32 entrera en mode download
    delay(100);
    
    // IMPORTANT: Initialiser USB HID SANS Serial pour éviter conflits
    // Compatible Windows, macOS, Linux, Android (USB OTG)
    
    // Initialiser USB HID - Compatible avec Android USB OTG
    USB.begin();
    delay(1000);  // Délai pour que USB soit initialisé
    
    // Initialiser Keyboard HID
    Keyboard.begin();
    delay(1000);  // Délai pour que le descripteur HID soit envoyé
    
    // Consumer Control est géré directement via USB.sendReport()
    // Pas besoin d'initialisation séparée
    
    // Attendre que le périphérique soit reconnu (important pour Android)
    // Android peut prendre un peu plus de temps pour reconnaître le périphérique HID
    delay(500);
    
    // Initialiser Bluetooth HID pour iOS/Android (si activé)
    #ifdef USE_BLE_KEYBOARD
    bleKeyboard.begin();
    outputMode = "usb";  // Par défaut USB, bascule automatiquement si BLE connecté
    #endif
    
    // Initialiser Serial pour débogage (si activé)
    // Note: Si DEBUG_USE_SERIAL0 est défini, utilise Serial0 (UART hardware)
    // Sinon, utilise Serial (port USB natif) - peut entrer en conflit avec USB HID
    #ifdef DEBUG_ENABLED
    DEBUG_SERIAL_BEGIN();
    delay(500);  // Délai plus long pour que Serial soit prêt
    DEBUG_PRINTLN("\n\n=== ESP32 Macropad Debug Mode ===");
    DEBUG_PRINTF("Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF("ATmega UART TX: GPIO%d, RX: GPIO%d, Baud: %d\n", ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD);
    DEBUG_PRINTF("Fingerprint UART TX: GPIO%d, RX: GPIO%d, Baud: %d\n", FINGERPRINT_UART_TX, FINGERPRINT_UART_RX, FINGERPRINT_UART_BAUD);
    #ifdef DEBUG_USE_SERIAL1
    DEBUG_PRINTLN("Using Serial1 (UART hardware) for debug");
    #elif defined(DEBUG_USE_SERIAL2)
    DEBUG_PRINTLN("Using Serial2 (UART hardware) for debug");
    #else
    DEBUG_PRINTLN("Using Serial (USB native) for debug");
    #endif
    #endif
    delay(100);
    
    // Initialiser préférences
    preferences.begin("macropad", false);
    DEBUG_PRINTLN("[SYSTEM] Preferences initialized");
    
    // Initialiser UART pour communication avec ATmega328P
    initUART();
    
    // Initialiser UART pour capteur d'empreinte digitale
    initFingerprintUART();
    
    // Initialiser la configuration de l'écran (l'écran est sur l'ATmega)
    initDisplay();
    
    // Envoyer les données d'affichage initiales à l'ATmega
    delay(500);  // Attendre que l'ATmega soit prêt
    sendDisplayDataToAtmega();
    DEBUG_PRINTLN("[DISPLAY] Initial display data sent to ATmega");
    
    // Charger la configuration du backlight
    backlightEnabled = preferences.getBool(PREF_BACKLIGHT_ENABLED, true);
    backlightBrightness = preferences.getUChar(PREF_BACKLIGHT_BRIGHTNESS, 128);
    autoBrightnessEnabled = preferences.getBool(PREF_AUTO_BRIGHTNESS, false);
    envBrightnessMode = preferences.getBool(PREF_ENV_BRIGHTNESS, false);
    
    // Configurer les pins de la matrice
    // Colonnes en sortie (HIGH par défaut)
    for (int i = 0; i < MATRIX_COLS; i++) {
        pinMode(matrixCols[i], OUTPUT);
        digitalWrite(matrixCols[i], HIGH);
    }
    
    // Lignes en entrée avec pull-up
    for (int i = 0; i < MATRIX_ROWS; i++) {
        pinMode(matrixRows[i], INPUT_PULLUP);
    }
    
    // Serial.println("Matrix pins configured");  // DÉSACTIVÉ
    
    // Initialiser encodeur rotatif
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    encoderLastState = digitalRead(ENCODER_CLK);
    // Serial.println("Rotary encoder initialized");  // DÉSACTIVÉ
    
    // Charger la configuration depuis la mémoire flash
    loadConfigFromFlash();
    
    // Initialiser rowKeys depuis le profil actif
    updateRowKeysFromProfile();
    
    // Charger la configuration de l'écran depuis les préférences
    displayConfig.brightness = preferences.getUChar(PREF_DISPLAY_BRIGHTNESS, 128);
    displayConfig.mode = preferences.getString(PREF_DISPLAY_MODE, "data");
    displayConfig.customData.showProfile = true;
    displayConfig.customData.showBattery = true;
    displayConfig.customData.showMode = true;
    displayConfig.customData.showKeys = true;
    displayConfig.customData.showBacklight = true;
    displayConfig.customData.showCustom1 = false;
    displayConfig.customData.showCustom2 = false;
    displayConfig.customData.customLine1 = "";
    displayConfig.customData.customLine2 = "";
    displayConfig.gifFrameCount = 0;
    displayConfig.currentGifFrame = 0;
    displayConfig.lastGifUpdate = 0;
    
    DEBUG_PRINTLN("=== Initialization Complete ===");
    DEBUG_PRINTLN("Macropad ready - 4x5 matrix + rotary encoder");
    DEBUG_PRINTF("Backlight enabled: %s\n", backlightEnabled ? "YES" : "NO");
    DEBUG_PRINTF("Env brightness mode: %s\n", envBrightnessMode ? "YES" : "NO");
    DEBUG_PRINTF("Backlight brightness: %d/255\n", backlightBrightness);
    
    // Test désactivé pour éviter d'envoyer des caractères non désirés
    // delay(1000);
    // Keyboard.print("HID OK");
    // delay(100);
}

// ==================== LOOP PRINCIPAL ====================

void loop() {
    // Gérer la communication série (USB)
    handleSerialCommunication();
    
    // Gérer les connexions BLE
    #ifdef USE_BLE_KEYBOARD
    bleKeyboard.loop();
    if (bleKeyboard.isConnected()) {
        outputMode = "bluetooth";
    } else {
        outputMode = "usb";
    }
    #endif
    
    // Mettre à jour l'écran OLED périodiquement
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate >= 100) {  // Mise à jour toutes les 100ms
        updateDisplay();
        lastDisplayUpdate = millis();
    }
    
    // Scanner les touches de la matrice
    if (millis() - lastKeyScan > 10) {
        scanKeys();
        lastKeyScan = millis();
    }
    
    // Gérer l'encodeur rotatif
    handleEncoder();
    
    // Gérer le backlight automatique basé sur la luminosité
    if (envBrightnessMode) {
        updateBacklightAuto();
    }
    
    delay(5);
}

// ==================== COMMUNICATION SÉRIE ====================

void handleSerialCommunication() {
    // DÉSACTIVÉ pour éviter conflit avec USB HID sur Android
    // Le web UI nécessite un câble UART séparé pour la configuration
    // if (Serial.available()) {
    //     String message = Serial.readStringUntil('\n');
    //     message.trim();
    //     
    //     if (message.length() > 0) {
    //         processWebMessage(message);
    //     }
    // }
}

void processWebMessage(String message) {
    DEBUG_WEB_PRINTF("[WEB_UI] Received: %s\n", message.c_str());
    
    if (message.startsWith("{\"type\":\"config\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Processing config message");
        parseConfigMessage(message);
    } else if (message.startsWith("{\"type\":\"backlight\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Processing backlight message");
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, message);
        if (!error && doc.containsKey("type") && doc["type"] == "backlight") {
            JsonObject backlightObj = doc.as<JsonObject>();
            handleBacklightConfig(backlightObj);
            sendStatusMessage("Backlight config updated");
            DEBUG_WEB_PRINTLN("[WEB_UI] Backlight config updated");
        } else {
            DEBUG_WEB_PRINTF("[WEB_UI] JSON parse error: %s\n", error.c_str());
        }
    } else if (message.startsWith("{\"type\":\"get_light\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Requesting light level");
        // Envoyer la luminosité ambiante
        uint16_t light = readAmbientLight();
        String response = "{\"type\":\"light\",\"level\":" + String(light) + "}";
        DEBUG_WEB_PRINTF("[WEB_UI] Light level: %d\n", light);
        // Note: sendStatusMessage pourrait être utilisé si Serial est activé
        // Pour l'instant, on stocke la valeur dans ambientLightLevel
        ambientLightLevel = light;
    } else if (message.startsWith("{\"type\":\"display\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Processing display message");
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, message);
        if (!error && doc.containsKey("type") && doc["type"] == "display") {
            JsonObject displayObj = doc.as<JsonObject>();
            handleDisplayConfig(displayObj);
            sendStatusMessage("Display config updated");
            DEBUG_WEB_PRINTLN("[WEB_UI] Display config updated");
        } else {
            DEBUG_WEB_PRINTF("[WEB_UI] JSON parse error: %s\n", error.c_str());
        }
    } else if (message.startsWith("{\"type\":\"settings\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Processing settings message");
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, message);
        if (!error && doc.containsKey("type") && doc["type"] == "settings") {
            JsonObject settingsObj = doc.as<JsonObject>();
            handleSettingsConfig(settingsObj);
            sendStatusMessage("Settings updated");
            DEBUG_WEB_PRINTLN("[WEB_UI] Settings updated");
        } else {
            DEBUG_WEB_PRINTF("[WEB_UI] JSON parse error: %s\n", error.c_str());
        }
    } else if (message.startsWith("{\"type\":\"display_image\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Processing display_image message");
        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, message);
        if (!error && doc.containsKey("type") && doc["type"] == "display_image") {
            JsonObject imageObj = doc.as<JsonObject>();
            handleDisplayImage(imageObj);
            DEBUG_WEB_PRINTLN("[WEB_UI] Display image updated");
        } else {
            DEBUG_WEB_PRINTF("[WEB_UI] JSON parse error: %s\n", error.c_str());
        }
    } else if (message.startsWith("{\"type\":\"status\"")) {
        DEBUG_WEB_PRINTLN("[WEB_UI] Status request");
        sendStatusMessage("Macropad ready");
    } else {
        DEBUG_WEB_PRINTLN("[WEB_UI] Unknown message type");
    }
}

void parseConfigMessage(String json) {
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        // Serial.print("JSON parse error: ");  // DÉSACTIVÉ
        // Serial.println(error.c_str());  // DÉSACTIVÉ
        sendStatusMessage("Config parse error");
        return;
    }
    
    // Parser les profils
    if (doc.containsKey("profiles")) {
        JsonObject profilesObj = doc["profiles"].as<JsonObject>();
        config.profileCount = 0;
        
        for (JsonPair profilePair : profilesObj) {
            if (config.profileCount >= MAX_PROFILES) break;
            
            String profileName = profilePair.key().c_str();
            JsonObject profileData = profilePair.value().as<JsonObject>();
            
            Profile& profile = config.profiles[config.profileCount];
            profile.name = profileName;
            
            // Parser les touches
            if (profileData.containsKey("keys")) {
                JsonObject keysObj = profileData["keys"].as<JsonObject>();
                
                // Initialiser toutes les touches à vide d'abord
                for (int i = 0; i < TOTAL_KEYS; i++) {
                    profile.keys[i].type = "";
                    profile.keys[i].value = "";
                    profile.keys[i].modifierCount = 0;
                    profile.keys[i].macroCount = 0;
                }
                
                // Parser chaque touche selon son keyId
                for (JsonPair keyPair : keysObj) {
                    String keyId = keyPair.key().c_str();
                    JsonObject keyData = keyPair.value().as<JsonObject>();
                    
                    // Convertir le keyId en index linéaire
                    int keyIndex = getIndexFromKeyId(keyId);
                    if (keyIndex < 0 || keyIndex >= TOTAL_KEYS) {
                        DEBUG_PRINTF("[CONFIG] KeyId invalide ignoré: %s\n", keyId.c_str());
                        continue;  // Ignorer les keyId invalides
                    }
                    
                    KeyConfig& key = profile.keys[keyIndex];
                    key.type = keyData["type"].as<String>();
                    key.value = keyData["value"].as<String>();
                    
                    // Parser modificateurs
                    if (keyData.containsKey("modifiers")) {
                        JsonArray mods = keyData["modifiers"].as<JsonArray>();
                        key.modifierCount = 0;
                        for (JsonVariant mod : mods) {
                            if (key.modifierCount >= MAX_MODIFIERS) break;
                            key.modifiers[key.modifierCount++] = mod.as<String>();
                        }
                    }
                    
                    // Parser macro
                    if (keyData.containsKey("macro")) {
                        JsonArray macro = keyData["macro"].as<JsonArray>();
                        key.macroCount = 0;
                        for (JsonVariant step : macro) {
                            if (key.macroCount >= MAX_MACRO_STEPS) break;
                            key.macro[key.macroCount++] = step.as<String>();
                        }
                    }
                    
                    if (keyData.containsKey("delay")) {
                        key.delay = keyData["delay"].as<int>();
                    } else {
                        key.delay = 50;
                    }
                }
            }
            
            config.profileCount++;
        }
    }
    
    // Parser le profil actif
    if (doc.containsKey("activeProfile")) {
        String activeProfileName = doc["activeProfile"].as<String>();
        for (int i = 0; i < config.profileCount; i++) {
            if (config.profiles[i].name == activeProfileName) {
                config.activeProfileIndex = i;
                break;
            }
        }
    }
    
    // Sauvegarder la configuration
    saveConfigToFlash();
    
    // Recharger la configuration depuis la flash pour s'assurer qu'elle est appliquée
    loadConfigFromFlash();
    
    // Mettre à jour rowKeys depuis le profil actif
    updateRowKeysFromProfile();
    
    // Envoyer confirmation
    sendStatusMessage("Config saved and loaded");
}

void sendStatusMessage(String message) {
    String json = "{\"type\":\"status\",\"message\":\"" + message + "\"}";
    
    // Envoyer via Serial si disponible
    #ifdef DEBUG_ENABLED
    DEBUG_PRINTLN(json);
    #endif
    
    // Envoyer via Bluetooth Serial si connecté
    #ifdef USE_BLE_KEYBOARD
    if (deviceConnected && pSerialCharacteristic != nullptr) {
        pSerialCharacteristic->setValue((json + "\n").c_str());
        pSerialCharacteristic->notify();
    }
    #endif
}

// ==================== SCAN DES TOUCHES ====================

void scanKeys() {
    // Scanner chaque colonne
    for (int col = 0; col < MATRIX_COLS; col++) {
        // Mettre la colonne à LOW
        digitalWrite(matrixCols[col], LOW);
        delayMicroseconds(50);  // Délai augmenté pour meilleure stabilité
        
        // Lire chaque ligne
        for (int row = 0; row < MATRIX_ROWS; row++) {
            // Vérifier si cette position (row, col) a une touche valide
            // Chaque row a un nombre différent de touches:
            // Row0: 4 touches (cols 0,1,2,3)
            // Row1: 4 touches (cols 0,1,2,3)
            // Row2: 3 touches (cols 0,1,2)
            // Row3: 4 touches (cols 0,1,2,3)
            // Row4: 2 touches (cols 0,2) - Note: col 1 et 3 n'existent pas physiquement
            if (row == 2 && col >= 3) continue;  // Row2 n'a que 3 touches (cols 0,1,2)
            if (row == 4 && (col == 1 || col == 3)) continue;  // Row4 n'a que 2 touches (cols 0,2)
            
            // Lire l'état de la ligne (LOW = touche pressée)
            bool pressed = (digitalRead(matrixRows[row]) == LOW);
            
            // Détecter les changements avec debounce amélioré
            unsigned long currentTime = millis();
            if (pressed && lastKeyState[row][col] == 0) {
                // Touche pressée - vérifier le debounce
                if (currentTime - lastKeyPressTime[row][col] > DEBOUNCE_TIME) {
                    lastKeyState[row][col] = 1;
                    lastKeyPressTime[row][col] = currentTime;
                    handleKeyPress(row, col, true);
                }
            } else if (!pressed && lastKeyState[row][col] == 1) {
                // Touche relâchée - vérifier le debounce
                if (currentTime - lastKeyPressTime[row][col] > DEBOUNCE_TIME) {
                    lastKeyState[row][col] = 0;
                    lastKeyPressTime[row][col] = currentTime;
                    handleKeyPress(row, col, false);
                }
            }
        }
        
        // Remettre la colonne à HIGH
        digitalWrite(matrixCols[col], HIGH);
        delayMicroseconds(50);  // Délai augmenté avant la prochaine colonne
    }
}

void handleKeyPress(int row, int col, bool pressed) {
    DEBUG_HID_PRINTF("[HID] Key [%d,%d] %s\n", row, col, pressed ? "PRESSED" : "RELEASED");
    if (!pressed) return; // Ne traiter que les pressions
    
    #ifdef USE_BLE_KEYBOARD
    bool useBLE = bleKeyboard.isConnected();
    #else
    bool useBLE = false;
    #endif
    
    // Toujours utiliser la configuration du web UI stockée dans la flash
    // Si pas de profil, créer un profil par défaut
    if (config.profileCount == 0) {
        loadConfigFromFlash();  // Recharger la config
    }
    
    // IMPORTANT: Profile switch UNIQUEMENT pour row0, col0 - vérifier AVANT de charger la config
    // Cela évite que d'autres touches de row0 déclenchent le switch même si mal configurées
    if (row == 0 && col == 0) {
        // C'est la touche profile switch - vérifier si elle est configurée comme telle
        updateRowKeysFromProfile();
        if (col < rowKeys[row].keyCount) {
            KeyConfig& key = rowKeys[row].keys[col];
            if (key.type == "profile" && key.value == "switch") {
                switchProfile();
                return;
            }
        }
        // Si pas configurée comme profile, continuer avec le traitement normal
    }
    
    // Mettre à jour rowKeys depuis le profil actif
    updateRowKeysFromProfile();
    
    // Obtenir la configuration de la touche depuis l'array de la row
    if (col < rowKeys[row].keyCount) {
        KeyConfig& key = rowKeys[row].keys[col];
        
        // Si la touche est configurée dans le web UI, utiliser cette configuration
        if (key.type != "" && key.value != "") {
            // Exécuter l'action selon le type (configuration du web UI)
            // PROTECTION: Ignorer profile switch si ce n'est pas row0, col0
            if (key.type == "profile" && key.value == "switch") {
                // Si on arrive ici et que ce n'est pas row0, col0, c'est une erreur de config
                if (row == 0 && col == 0) {
                    switchProfile();
                } else {
                    DEBUG_HID_PRINTF("[HID] ERREUR: Profile switch configuré sur [%d,%d] mais ignoré (seulement row0,col0)\n", row, col);
                    // Traiter comme une touche non configurée
                }
                return;
            } else if (key.type == "key") {
                sendKey(key);
            } else if (key.type == "modifier") {
                sendModifier(key);
            } else if (key.type == "media") {
                sendMediaKey(key);
            } else if (key.type == "macro") {
                executeMacro(key);
            }
            return;
        }
    }
    
    // Si pas de configuration, utiliser valeur par défaut
    String defaultValue = String(row * 10 + col);
    if (useBLE) bleKeyboard.write(defaultValue.charAt(0));
    else Keyboard.write(defaultValue.charAt(0));
}

// Fonction pour mettre à jour rowKeys depuis le profil actif
void updateRowKeysFromProfile() {
    if (config.profileCount == 0) return;
    
    Profile& profile = config.profiles[config.activeProfileIndex];
    
    // Row0 (GPIO4): [profileswitch, /, *, 0] - 4 éléments (cols 0,1,2,3)
    rowKeys[0].keyCount = 4;
    rowKeys[0].keys[0] = profile.keys[0];   // col 0
    rowKeys[0].keys[1] = profile.keys[1];   // col 1
    rowKeys[0].keys[2] = profile.keys[2];   // col 2
    rowKeys[0].keys[3] = profile.keys[3];   // col 3
    
    // Row1 (GPIO5): [7, 8, 9, +] - 4 éléments (cols 0,1,2,3)
    rowKeys[1].keyCount = 4;
    rowKeys[1].keys[0] = profile.keys[4];   // col 0
    rowKeys[1].keys[1] = profile.keys[5];   // col 1
    rowKeys[1].keys[2] = profile.keys[6];   // col 2
    rowKeys[1].keys[3] = profile.keys[7];   // col 3
    
    // Row2 (GPIO6): [5, 6, 7] - 3 éléments (cols 0,1,2)
    rowKeys[2].keyCount = 3;
    rowKeys[2].keys[0] = profile.keys[8];   // col 0
    rowKeys[2].keys[1] = profile.keys[9];   // col 1
    rowKeys[2].keys[2] = profile.keys[10];  // col 2
    
    // Row3 (GPIO7): [1, 2, 3, =] - 4 éléments (cols 0,1,2,3)
    rowKeys[3].keyCount = 4;
    rowKeys[3].keys[0] = profile.keys[12];  // col 0
    rowKeys[3].keys[1] = profile.keys[13];  // col 1
    rowKeys[3].keys[2] = profile.keys[14];  // col 2
    rowKeys[3].keys[3] = profile.keys[15];  // col 3
    
    // Row4 (GPIO15): [0, .] - 2 éléments (cols 0,2)
    rowKeys[4].keyCount = 2;
    rowKeys[4].keys[0] = profile.keys[16];  // col 0
    rowKeys[4].keys[2] = profile.keys[18];  // col 2 (note: col 1 et 3 n'existent pas)
}

void sendKey(KeyConfig& key) {
    #ifdef USE_BLE_KEYBOARD
    bool useBLE = bleKeyboard.isConnected();
    #else
    bool useBLE = false;
    #endif
    
    DEBUG_HID_PRINTF("[HID] Sending key: type=%s, value=%s, modifiers=%d\n", 
                      key.type.c_str(), key.value.c_str(), key.modifierCount);
    
    // Appuyer sur les modificateurs
    if (key.modifierCount > 0) {
        for (int i = 0; i < key.modifierCount; i++) {
            if (key.modifiers[i] == "CTRL") {
                if (useBLE) bleKeyboard.press(KEY_LEFT_CTRL);
                else Keyboard.press(KEY_LEFT_CTRL);
            }
            else if (key.modifiers[i] == "SHIFT") {
                if (useBLE) bleKeyboard.press(KEY_LEFT_SHIFT);
                else Keyboard.press(KEY_LEFT_SHIFT);
            }
            else if (key.modifiers[i] == "ALT") {
                if (useBLE) bleKeyboard.press(KEY_LEFT_ALT);
                else Keyboard.press(KEY_LEFT_ALT);
            }
            else if (key.modifiers[i] == "GUI") {
                if (useBLE) bleKeyboard.press(KEY_LEFT_GUI);
                else Keyboard.press(KEY_LEFT_GUI);
            }
        }
    }
    
    // Envoyer la touche
    sendKeyValue(key.value);
    
    // Relâcher les modificateurs
    if (key.modifierCount > 0) {
        if (useBLE) bleKeyboard.releaseAll();
        else Keyboard.releaseAll();
    }
}

void sendKeyValue(String value) {
    // Convertir la valeur en code de touche
    // Utilise USB HID par défaut, BLE HID si connecté (compatible Windows/macOS/Linux/iOS/Android)
    
    #ifdef USE_BLE_KEYBOARD
    bool useBLE = bleKeyboard.isConnected();
    #else
    bool useBLE = false;
    #endif
    
    if (value == "ENTER" || value == "RETURN") {
        if (useBLE) bleKeyboard.write(KEY_RETURN);
        else Keyboard.write(KEY_RETURN);
    }
    else if (value == "SPACE") {
        if (useBLE) bleKeyboard.write(' ');
        else Keyboard.write(' ');
    }
    else if (value == "TAB") {
        if (useBLE) bleKeyboard.write(KEY_TAB);
        else Keyboard.write(KEY_TAB);
    }
    else if (value == "ESC" || value == "ESCAPE") {
        if (useBLE) bleKeyboard.write(KEY_ESC);
        else Keyboard.write(KEY_ESC);
    }
    else if (value == "BACKSPACE") {
        if (useBLE) bleKeyboard.write(KEY_BACKSPACE);
        else Keyboard.write(KEY_BACKSPACE);
    }
    else if (value == "DELETE") {
        if (useBLE) bleKeyboard.write(KEY_DELETE);
        else Keyboard.write(KEY_DELETE);
    }
    else if (value == "UP") {
        if (useBLE) bleKeyboard.write(KEY_UP_ARROW);
        else Keyboard.write(KEY_UP_ARROW);
    }
    else if (value == "DOWN") {
        if (useBLE) bleKeyboard.write(KEY_DOWN_ARROW);
        else Keyboard.write(KEY_DOWN_ARROW);
    }
    else if (value == "LEFT") {
        if (useBLE) bleKeyboard.write(KEY_LEFT_ARROW);
        else Keyboard.write(KEY_LEFT_ARROW);
    }
    else if (value == "RIGHT") {
        if (useBLE) bleKeyboard.write(KEY_RIGHT_ARROW);
        else Keyboard.write(KEY_RIGHT_ARROW);
    }
    else if (value == "F1") {
        if (useBLE) { bleKeyboard.press(KEY_F1); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F1); Keyboard.releaseAll(); }
    }
    else if (value == "F2") {
        if (useBLE) { bleKeyboard.press(KEY_F2); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F2); Keyboard.releaseAll(); }
    }
    else if (value == "F3") {
        if (useBLE) { bleKeyboard.press(KEY_F3); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F3); Keyboard.releaseAll(); }
    }
    else if (value == "F4") {
        if (useBLE) { bleKeyboard.press(KEY_F4); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F4); Keyboard.releaseAll(); }
    }
    else if (value == "F5") {
        if (useBLE) { bleKeyboard.press(KEY_F5); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F5); Keyboard.releaseAll(); }
    }
    else if (value == "F6") {
        if (useBLE) { bleKeyboard.press(KEY_F6); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F6); Keyboard.releaseAll(); }
    }
    else if (value == "F7") {
        if (useBLE) { bleKeyboard.press(KEY_F7); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F7); Keyboard.releaseAll(); }
    }
    else if (value == "F8") {
        if (useBLE) { bleKeyboard.press(KEY_F8); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F8); Keyboard.releaseAll(); }
    }
    else if (value == "F9") {
        if (useBLE) { bleKeyboard.press(KEY_F9); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F9); Keyboard.releaseAll(); }
    }
    else if (value == "F10") {
        if (useBLE) { bleKeyboard.press(KEY_F10); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F10); Keyboard.releaseAll(); }
    }
    else if (value == "F11") {
        if (useBLE) { bleKeyboard.press(KEY_F11); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F11); Keyboard.releaseAll(); }
    }
    else if (value == "F12") {
        if (useBLE) { bleKeyboard.press(KEY_F12); bleKeyboard.releaseAll(); }
        else { Keyboard.press(KEY_F12); Keyboard.releaseAll(); }
    }
    else if (value.length() == 1) {
        if (useBLE) bleKeyboard.write(value.charAt(0));
        else Keyboard.write(value.charAt(0));
    }
}

void sendModifier(KeyConfig& key) {
    sendKeyValue(key.value);
}

void sendMediaKey(KeyConfig& key) {
    // Les touches média utilisent Consumer Control
    // Pour l'instant, on les ignore (peut être implémenté si nécessaire)
    // Serial.println("Media keys not implemented yet");  // DÉSACTIVÉ
}

void executeMacro(KeyConfig& key) {
    for (int i = 0; i < key.macroCount; i++) {
        String macroStep = key.macro[i];
        // Parser simple : "CTRL+C" -> CTRL + C
        if (macroStep.indexOf("CTRL+") >= 0) {
            Keyboard.press(KEY_LEFT_CTRL);
            String keyPart = macroStep.substring(5);
            sendKeyValue(keyPart);
            Keyboard.releaseAll();
        } else {
            sendKeyValue(macroStep);
        }
        delay(key.delay);
    }
}

// ==================== GESTION DES PROFILS ====================

void switchProfile() {
    // Passer au profil suivant
    if (config.profileCount > 0) {
        config.activeProfileIndex = (config.activeProfileIndex + 1) % config.profileCount;
        preferences.putInt(PREF_ACTIVE_PROFILE, config.activeProfileIndex);
        // Mettre à jour rowKeys depuis le nouveau profil actif
        updateRowKeysFromProfile();
        // Optionnel: envoyer un feedback visuel ou sonore
    }
}

// ==================== ENCODEUR ROTATIF ====================

void handleEncoder() {
    // Lire l'état actuel de CLK
    int clkState = digitalRead(ENCODER_CLK);
    int dtState = digitalRead(ENCODER_DT);
    int swState = digitalRead(ENCODER_SW);
    
    // Détection de rotation avec debounce amélioré
    if (millis() - lastEncoderRead > 5) {  // Délai augmenté pour meilleure stabilité
        if (clkState != encoderLastState) {
            // Rotation détectée - vérifier la direction
            if (dtState != clkState) {
                // Rotation horaire (volume up) - CLK change avant DT
                encoderPosition++;
                adjustVolume(1);  // Volume UP
            } else {
                // Rotation anti-horaire (volume down) - DT change avant CLK
                encoderPosition--;
                adjustVolume(-1);  // Volume DOWN
            }
            lastEncoderRead = millis();
        }
        encoderLastState = clkState;
    }
    
    // Détection du bouton (mute) avec debounce
    static unsigned long lastButtonPress = 0;
    if (swState == LOW && !encoderButtonPressed && (millis() - lastButtonPress > 200)) {
        encoderButtonPressed = true;
        lastButtonPress = millis();
        toggleMute();
    } else if (swState == HIGH) {
        encoderButtonPressed = false;
    }
}

void adjustVolume(int direction) {
    DEBUG_HID_PRINTF("[HID] Volume adjustment: %s\n", direction > 0 ? "UP" : "DOWN");
    
    // Envoyer directement les codes Consumer Control HID
    // Utilise ConsumerControl::press() et ConsumerControl::release() comme dans l'exemple de NicoHood
    // Référence: https://github.com/alxsch/mymacrokeyboard/blob/main/src/main.cpp
    if (direction > 0) {
        // Volume up - utiliser ConsumerControl::press() puis ConsumerControl::release()
        ConsumerControl::press(MEDIA_VOLUME_UP);
        ConsumerControl::release(MEDIA_VOLUME_UP);
        delay(30);  // Petit délai pour éviter les répétitions trop rapides
    } else {
        // Volume down - utiliser ConsumerControl::press() puis ConsumerControl::release()
        ConsumerControl::press(MEDIA_VOLUME_DOWN);
        ConsumerControl::release(MEDIA_VOLUME_DOWN);
        delay(30);  // Petit délai pour éviter les répétitions trop rapides
    }
}

void toggleMute() {
    DEBUG_HID_PRINTLN("[HID] Mute toggle");
    
    // Envoyer la commande Consumer Control Mute - utiliser ConsumerControl::press() puis ConsumerControl::release()
    // Référence: https://github.com/alxsch/mymacrokeyboard/blob/main/src/main.cpp
    ConsumerControl::press(MEDIA_VOLUME_MUTE);
    ConsumerControl::release(MEDIA_VOLUME_MUTE);
    delay(30);  // Petit délai pour éviter les répétitions trop rapides
}



// ==================== STOCKAGE EN MÉMOIRE FLASH ====================

void loadConfigFromFlash() {
    // Serial.println("Loading config from flash...");  // DÉSACTIVÉ
    
    config.profileCount = preferences.getInt(PREF_PROFILE_COUNT, 0);
    config.activeProfileIndex = preferences.getInt(PREF_ACTIVE_PROFILE, 0);
    
    // Pour réinitialiser la configuration (si les touches ne fonctionnent pas correctement)
    // Changez forceReset à true, téléversez, puis remettez à false
    bool forceReset = false;  // Mettre à true pour réinitialiser complètement la config
    bool forceResetRow0 = true;  // Mettre à true pour forcer la réinitialisation de row0 uniquement
    
    if (config.profileCount == 0 || forceReset) {
        // Réinitialiser complètement la configuration
        if (forceReset) {
            preferences.clear();  // Effacer toutes les préférences
        }
        
        config.profileCount = 1;
        config.profiles[0].name = "Profile 1";
        config.activeProfileIndex = 0;
        
        // Initialiser toutes les touches à vide d'abord
        for (int i = 0; i < TOTAL_KEYS; i++) {
            config.profiles[0].keys[i].type = "";
            config.profiles[0].keys[i].value = "";
            config.profiles[0].keys[i].modifierCount = 0;
            config.profiles[0].keys[i].macroCount = 0;
        }
    } else if (forceResetRow0) {
        // Réinitialiser uniquement row0 pour corriger le problème de profile switch
        DEBUG_PRINTLN("[CONFIG] Réinitialisation forcée de row0");
        // Réinitialiser les touches de row0 (indices 0, 1, 2, 3)
        for (int i = 0; i < 4; i++) {
            config.profiles[config.activeProfileIndex].keys[i].type = "";
            config.profiles[config.activeProfileIndex].keys[i].value = "";
            config.profiles[config.activeProfileIndex].keys[i].modifierCount = 0;
            config.profiles[config.activeProfileIndex].keys[i].macroCount = 0;
        }
        
        // Configuration du profil par défaut selon le nouveau système row/col
        // Row0 (GPIO4): [profileswitch, /, *, 0] - 4 éléments (cols 0,1,2,3)
        config.profiles[0].keys[0].type = "profile";  // Row0, Col0 = Profile switch
        config.profiles[0].keys[0].value = "switch";
        config.profiles[0].keys[1].type = "key";  // Row0, Col1 = "/"
        config.profiles[0].keys[1].value = "/";
        config.profiles[0].keys[2].type = "key";  // Row0, Col2 = "*"
        config.profiles[0].keys[2].value = "*";
        config.profiles[0].keys[3].type = "key";  // Row0, Col3 = "0"
        config.profiles[0].keys[3].value = "0";
        
        saveConfigToFlash();
    } else if (forceResetRow0) {
        // Après réinitialisation de row0, reconfigurer avec les valeurs par défaut
        // Row0 (GPIO4): [profileswitch, /, *, 0] - 4 éléments (cols 0,1,2,3)
        config.profiles[config.activeProfileIndex].keys[0].type = "profile";  // Row0, Col0 = Profile switch
        config.profiles[config.activeProfileIndex].keys[0].value = "switch";
        config.profiles[config.activeProfileIndex].keys[1].type = "key";  // Row0, Col1 = "/"
        config.profiles[config.activeProfileIndex].keys[1].value = "/";
        config.profiles[config.activeProfileIndex].keys[2].type = "key";  // Row0, Col2 = "*"
        config.profiles[config.activeProfileIndex].keys[2].value = "*";
        config.profiles[config.activeProfileIndex].keys[3].type = "key";  // Row0, Col3 = "0"
        config.profiles[config.activeProfileIndex].keys[3].value = "0";
        
        saveConfigToFlash();
        DEBUG_PRINTLN("[CONFIG] Row0 réinitialisée et sauvegardée");
    }
    
    // Continuer avec la configuration des autres rows si c'était un reset complet
    if (config.profileCount == 0 || forceReset) {
        // Row1 (GPIO5): [7, 8, 9, +] - 4 éléments (cols 0,1,2,3)
        config.profiles[0].keys[4].type = "key";  // Row1, Col0 = "7"
        config.profiles[0].keys[4].value = "7";
        config.profiles[0].keys[5].type = "key";  // Row1, Col1 = "8"
        config.profiles[0].keys[5].value = "8";
        config.profiles[0].keys[6].type = "key";  // Row1, Col2 = "9"
        config.profiles[0].keys[6].value = "9";
        config.profiles[0].keys[7].type = "key";  // Row1, Col3 = "+"
        config.profiles[0].keys[7].value = "+";
        
        // Row2 (GPIO6): [5, 6, 7] - 3 éléments (cols 0,1,2)
        config.profiles[0].keys[8].type = "key";  // Row2, Col0 = "5"
        config.profiles[0].keys[8].value = "5";
        config.profiles[0].keys[9].type = "key";  // Row2, Col1 = "6"
        config.profiles[0].keys[9].value = "6";
        config.profiles[0].keys[10].type = "key";  // Row2, Col2 = "7"
        config.profiles[0].keys[10].value = "7";
        
        // Row3 (GPIO7): [1, 2, 3, =] - 4 éléments (cols 0,1,2,3)
        config.profiles[0].keys[12].type = "key";  // Row3, Col0 = "1"
        config.profiles[0].keys[12].value = "1";
        config.profiles[0].keys[13].type = "key";  // Row3, Col1 = "2"
        config.profiles[0].keys[13].value = "2";
        config.profiles[0].keys[14].type = "key";  // Row3, Col2 = "3"
        config.profiles[0].keys[14].value = "3";
        config.profiles[0].keys[15].type = "key";  // Row3, Col3 = "="
        config.profiles[0].keys[15].value = "=";
        
        // Row4 (GPIO15): [0, .] - 2 éléments (cols 0,2)
        config.profiles[0].keys[16].type = "key";  // Row4, Col0 = "0"
        config.profiles[0].keys[16].value = "0";
        config.profiles[0].keys[18].type = "key";  // Row4, Col2 = "."
        config.profiles[0].keys[18].value = ".";
        
        saveConfigToFlash();
    }
    } else {
        // Charger chaque profil
        for (int i = 0; i < config.profileCount && i < MAX_PROFILES; i++) {
            String profileKey = String(PREF_PROFILE_PREFIX) + String(i);
            String profileData = preferences.getString(profileKey.c_str(), "");
            
            if (profileData.length() > 0) {
                parseProfileFromString(profileData, config.profiles[i]);
                // Serial.print("Loaded profile: ");  // DÉSACTIVÉ
                // Serial.println(config.profiles[i].name);  // DÉSACTIVÉ
                
                // Protection: S'assurer que seule la touche [0] (row0, col0) est configurée comme profile switch
                // Corriger toute configuration incorrecte pour les touches de row0
                // Row0 correspond aux indices 0, 1, 2, 3 dans profile.keys[]
                for (int keyIdx = 1; keyIdx < 4; keyIdx++) {  // Seulement les touches 1, 2, 3 de row0
                    if (config.profiles[i].keys[keyIdx].type == "profile" && 
                        config.profiles[i].keys[keyIdx].value == "switch") {
                        // Cette touche est dans row0 mais n'est pas col0, donc on la réinitialise
                        DEBUG_PRINTF("[CONFIG] Corrigé: touche row0,col%d (index %d) avait type=profile, réinitialisée\n", keyIdx, keyIdx);
                        config.profiles[i].keys[keyIdx].type = "";
                        config.profiles[i].keys[keyIdx].value = "";
                    }
                }
                
                // Protection supplémentaire: S'assurer que les autres touches n'ont pas type=profile
                for (int keyIdx = 4; keyIdx < TOTAL_KEYS; keyIdx++) {
                    if (config.profiles[i].keys[keyIdx].type == "profile") {
                        DEBUG_PRINTF("[CONFIG] Corrigé: touche index %d avait type=profile, réinitialisée\n", keyIdx);
                        config.profiles[i].keys[keyIdx].type = "";
                        config.profiles[i].keys[keyIdx].value = "";
                    }
                }
                
                // Sauvegarder la configuration corrigée
                saveConfigToFlash();
            } else {
                config.profiles[i].name = "Profil " + String(i + 1);
            }
        }
    }
    
    config.outputMode = preferences.getString(PREF_OUTPUT_MODE, "usb");
    
    // Serial.print("Loaded ");  // DÉSACTIVÉ
    // Serial.print(config.profileCount);  // DÉSACTIVÉ
    // Serial.println(" profiles from flash");  // DÉSACTIVÉ
}

void saveConfigToFlash() {
    // Serial.println("Saving config to flash...");  // DÉSACTIVÉ
    
    preferences.putInt(PREF_PROFILE_COUNT, config.profileCount);
    preferences.putInt(PREF_ACTIVE_PROFILE, config.activeProfileIndex);
    
    // Sauvegarder chaque profil
    for (int i = 0; i < config.profileCount && i < MAX_PROFILES; i++) {
        String profileKey = String(PREF_PROFILE_PREFIX) + String(i);
        String profileData = serializeProfileToString(config.profiles[i]);
        
        if (profileData.length() > 4000) {
            // Serial.print("Warning: Profile ");  // DÉSACTIVÉ
            // Serial.print(i);  // DÉSACTIVÉ
            // Serial.println(" too large, truncating...");  // DÉSACTIVÉ
            profileData = profileData.substring(0, 4000);
        }
        
        preferences.putString(profileKey.c_str(), profileData);
    }
    
    preferences.putString(PREF_OUTPUT_MODE, config.outputMode);
    
    // Serial.println("Config saved to flash successfully");  // DÉSACTIVÉ
}

String serializeProfileToString(Profile& profile) {
    StaticJsonDocument<1024> doc;
    doc["name"] = profile.name;
    
    JsonObject keysObj = doc.createNestedObject("keys");
    
    for (int i = 0; i < TOTAL_KEYS; i++) {
        KeyConfig& key = profile.keys[i];
        if (key.type == "") continue;
        
        String keyId = getKeyIdFromIndex(i);
        JsonObject keyObj = keysObj.createNestedObject(keyId);
        
        keyObj["type"] = key.type;
        keyObj["value"] = key.value;
        
        if (key.modifierCount > 0) {
            JsonArray mods = keyObj.createNestedArray("modifiers");
            for (int j = 0; j < key.modifierCount; j++) {
                mods.add(key.modifiers[j]);
            }
        }
        
        if (key.macroCount > 0) {
            JsonArray macro = keyObj.createNestedArray("macro");
            for (int j = 0; j < key.macroCount; j++) {
                macro.add(key.macro[j]);
            }
            keyObj["delay"] = key.delay;
        }
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

void parseProfileFromString(String json, Profile& profile) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        // Serial.print("Profile parse error: ");  // DÉSACTIVÉ
        // Serial.println(error.c_str());  // DÉSACTIVÉ
        return;
    }
    
    profile.name = doc["name"].as<String>();
    
    if (doc.containsKey("keys")) {
        JsonObject keysObj = doc["keys"].as<JsonObject>();
        int keyIndex = 0;
        
        for (JsonPair keyPair : keysObj) {
            if (keyIndex >= TOTAL_KEYS) break;
            
            JsonObject keyData = keyPair.value().as<JsonObject>();
            KeyConfig& key = profile.keys[keyIndex];
            
            key.type = keyData["type"].as<String>();
            key.value = keyData["value"].as<String>();
            
            if (keyData.containsKey("modifiers")) {
                JsonArray mods = keyData["modifiers"].as<JsonArray>();
                key.modifierCount = 0;
                for (JsonVariant mod : mods) {
                    if (key.modifierCount >= MAX_MODIFIERS) break;
                    key.modifiers[key.modifierCount++] = mod.as<String>();
                }
            } else {
                key.modifierCount = 0;
            }
            
            if (keyData.containsKey("macro")) {
                JsonArray macro = keyData["macro"].as<JsonArray>();
                key.macroCount = 0;
                for (JsonVariant step : macro) {
                    if (key.macroCount >= MAX_MACRO_STEPS) break;
                    key.macro[key.macroCount++] = step.as<String>();
                }
                key.delay = keyData.containsKey("delay") ? keyData["delay"].as<int>() : 50;
            } else {
                key.macroCount = 0;
            }
            
            keyIndex++;
        }
    }
}

String getKeyIdFromIndex(int index) {
    // Convertir l'index en ID de touche (ex: 0 -> "0-0", 1 -> "0-1", 2 -> "1-0", 3 -> "1-1")
    int row = index / MATRIX_COLS;
    int col = index % MATRIX_COLS;
    return String(row) + "-" + String(col);
}

int getIndexFromKeyId(String keyId) {
    // Convertir un keyId (ex: "0-1") en index linéaire selon le mapping row/col
    // Format: "row-col" (ex: "0-0", "0-1", "1-0", etc.)
    int dashPos = keyId.indexOf('-');
    if (dashPos < 0) return -1;  // Format invalide
    
    int row = keyId.substring(0, dashPos).toInt();
    int col = keyId.substring(dashPos + 1).toInt();
    
    // Vérifier que row et col sont valides
    if (row < 0 || row >= MATRIX_ROWS || col < 0 || col >= MATRIX_COLS) {
        return -1;  // Position invalide
    }
    
    // Mapping selon la structure réelle:
    // Row0 (indices 0-3): [0-0, 0-1, 0-2, 0-3] -> indices 0, 1, 2, 3
    // Row1 (indices 4-7): [1-0, 1-1, 1-2, 1-3] -> indices 4, 5, 6, 7
    // Row2 (indices 8-10): [2-0, 2-1, 2-2] -> indices 8, 9, 10 (pas de 2-3)
    // Row3 (indices 12-15): [3-0, 3-1, 3-2, 3-3] -> indices 12, 13, 14, 15 (pas de 11)
    // Row4 (indices 16, 18): [4-0, 4-2] -> indices 16, 18 (pas de 4-1, 4-3)
    
    if (row == 0) {
        return col;  // 0-3
    } else if (row == 1) {
        return 4 + col;  // 4-7
    } else if (row == 2) {
        if (col >= 3) return -1;  // Row2 n'a que 3 colonnes
        return 8 + col;  // 8-10
    } else if (row == 3) {
        return 12 + col;  // 12-15 (on saute l'index 11)
    } else if (row == 4) {
        if (col == 0) return 16;
        else if (col == 2) return 18;
        else return -1;  // Row4 n'a que col 0 et col 2
    }
    
    return -1;  // Row invalide
}

// ==================== COMMUNICATION UART AVEC ATmega328P ====================

void initUART() {
    SerialAtmega.begin(ATMEGA_UART_BAUD, SERIAL_8N1, ATMEGA_UART_RX, ATMEGA_UART_TX);
    delay(100);  // Délai pour que UART soit stable
    DEBUG_PRINTF("[UART] ATmega UART initialized - TX: GPIO%d, RX: GPIO%d, Baud: %d\n", 
                 ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD);
    
    // Vérifier la présence de l'ATmega328P en envoyant une commande de test
    delay(100);
    SerialAtmega.write(CMD_GET_LED);
    SerialAtmega.write('\n');
    delay(50);
    
    if (SerialAtmega.available() > 0) {
        DEBUG_PRINTLN("[UART] ATmega328P detected and responding!");
        while (SerialAtmega.available()) SerialAtmega.read();  // Nettoyer le buffer
    } else {
        DEBUG_PRINTLN("[UART] WARNING: ATmega328P may not be responding");
        DEBUG_PRINTLN("[UART] Possible causes:");
        DEBUG_PRINTLN("[UART]   - ATmega328P not powered");
        DEBUG_PRINTLN("[UART]   - UART wires not connected correctly");
        DEBUG_PRINTLN("[UART]   - Wrong baud rate");
        DEBUG_PRINTLN("[UART]   - ATmega328P not programmed");
    }
}

void initFingerprintUART() {
    SerialFingerprint.begin(FINGERPRINT_UART_BAUD, SERIAL_8N1, FINGERPRINT_UART_RX, FINGERPRINT_UART_TX);
    delay(100);
    DEBUG_PRINTF("[UART] Fingerprint UART initialized - TX: GPIO%d, RX: GPIO%d, Baud: %d\n",
                 FINGERPRINT_UART_TX, FINGERPRINT_UART_RX, FINGERPRINT_UART_BAUD);
}

uint16_t readAmbientLight() {
    DEBUG_PRINTLN("[UART] Reading ambient light from ATmega...");
    
    // Envoyer la commande
    SerialAtmega.write(CMD_READ_LIGHT);
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    // Attendre la réponse (format: [LOW_BYTE] [HIGH_BYTE] [\n])
    delay(20);
    
    if (SerialAtmega.available() < 2) {
        DEBUG_PRINTLN("[UART] ERROR: No data received from ATmega");
        return 0;
    }
    
    uint16_t light = SerialAtmega.read();
    light |= (SerialAtmega.read() << 8);
    
    // Lire le caractère de fin de ligne s'il est présent
    if (SerialAtmega.available() > 0) {
        char c = SerialAtmega.read();
        if (c != '\n' && c != '\r') {
            // Peut-être un byte supplémentaire, l'ignorer
        }
    }
    
    DEBUG_LIGHT_PRINTF("[LIGHT] Ambient light level: %d (0x%04X)\n", light, light);
    
    return light;
}

void setAtmegaLEDBrightness(uint8_t brightness) {
    DEBUG_PRINTF("[UART] Setting LED brightness to %d/255 (0x%02X)...\n", brightness, brightness);
    
    SerialAtmega.write(CMD_SET_LED);
    SerialAtmega.write(brightness);
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    backlightBrightness = brightness;
    DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] LED brightness set to %d/255\n", brightness);
}

// ==================== LOGIQUE BACKLIGHT AUTOMATIQUE ====================

void updateBacklightAuto() {
    unsigned long currentTime = millis();
    
    // Lire la luminosité ambiante périodiquement
    if (currentTime - lastLightRead >= LIGHT_READ_INTERVAL) {
        ambientLightLevel = readAmbientLight();
        lastLightRead = currentTime;
        
        DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Auto update - Light: %d, Thresholds: LOW=%d, HIGH=%d\n", 
                               ambientLightLevel, LIGHT_THRESHOLD_LOW, LIGHT_THRESHOLD_HIGH);
        
        // Logique automatique : si lumière élevée → désactiver LED, si faible → activer
        if (ambientLightLevel > LIGHT_THRESHOLD_HIGH) {
            // Lumière élevée → désactiver le backlight
            DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] High light detected - DISABLING LED");
            setAtmegaLEDBrightness(0);
            backlightEnabled = false;
        } else if (ambientLightLevel < LIGHT_THRESHOLD_LOW) {
            // Lumière faible → activer le backlight à la luminosité configurée
            DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] Low light detected - ENABLING LED");
            if (!backlightEnabled) {
                backlightEnabled = true;
            }
            setAtmegaLEDBrightness(backlightBrightness);
        } else {
            // Zone intermédiaire → ajuster progressivement
            DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] Intermediate light - ADJUSTING brightness");
            // Calculer la luminosité ajustée (inversement proportionnel)
            int16_t diff = ambientLightLevel - LIGHT_THRESHOLD_LOW;
            int16_t range = LIGHT_THRESHOLD_HIGH - LIGHT_THRESHOLD_LOW;
            uint8_t adjustedBrightness = backlightBrightness - ((diff * backlightBrightness) / range);
            if (adjustedBrightness > backlightBrightness) adjustedBrightness = 0;  // Protection overflow
            DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Adjusted brightness: %d/255\n", adjustedBrightness);
            setAtmegaLEDBrightness(adjustedBrightness);
            backlightEnabled = (adjustedBrightness > 0);
        }
    }
}

void handleBacklightConfig(JsonObject& backlightObj) {
    DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] Processing configuration...");
    
    if (backlightObj.containsKey("enabled")) {
        backlightEnabled = backlightObj["enabled"].as<bool>();
        preferences.putBool(PREF_BACKLIGHT_ENABLED, backlightEnabled);
        DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Enabled: %s\n", backlightEnabled ? "YES" : "NO");
    }
    
    if (backlightObj.containsKey("brightness")) {
        backlightBrightness = backlightObj["brightness"].as<uint8_t>();
        preferences.putUChar(PREF_BACKLIGHT_BRIGHTNESS, backlightBrightness);
        DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Brightness: %d/255\n", backlightBrightness);
    }
    
    if (backlightObj.containsKey("autoBrightness")) {
        autoBrightnessEnabled = backlightObj["autoBrightness"].as<bool>();
        preferences.putBool(PREF_AUTO_BRIGHTNESS, autoBrightnessEnabled);
        DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Auto brightness: %s\n", autoBrightnessEnabled ? "YES" : "NO");
    }
    
    if (backlightObj.containsKey("envBrightness")) {
        envBrightnessMode = backlightObj["envBrightness"].as<bool>();
        preferences.putBool(PREF_ENV_BRIGHTNESS, envBrightnessMode);
        DEBUG_BACKLIGHT_PRINTF("[BACKLIGHT] Environment mode: %s\n", envBrightnessMode ? "YES" : "NO");
    }
    
    // Appliquer la configuration
    if (envBrightnessMode) {
        // Mode automatique selon l'environnement
        DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] Applying environment-based auto mode");
        updateBacklightAuto();
    } else {
        // Mode manuel : utiliser la luminosité configurée
        DEBUG_BACKLIGHT_PRINTLN("[BACKLIGHT] Applying manual mode");
        if (backlightEnabled) {
            setAtmegaLEDBrightness(backlightBrightness);
        } else {
            setAtmegaLEDBrightness(0);
        }
    }
}

// ==================== GESTION ÉCRAN (via ATmega) ====================

void initDisplay() {
    DEBUG_PRINTLN("[DISPLAY] Initializing display configuration...");
    // L'écran physique est sur l'ATmega, on initialise juste la configuration
    displayConfig.brightness = 128;
    displayConfig.mode = "data";
    displayConfig.customData.showProfile = true;
    displayConfig.customData.showBattery = true;
    displayConfig.customData.showMode = true;
    displayConfig.customData.showKeys = true;
    displayConfig.customData.showBacklight = true;
    displayConfig.customData.showCustom1 = false;
    displayConfig.customData.showCustom2 = false;
    displayConfig.customData.customLine1 = "";
    displayConfig.customData.customLine2 = "";
    displayConfig.gifFrameCount = 0;
    displayConfig.currentGifFrame = 0;
    displayConfig.lastGifUpdate = 0;
    
    // Initialiser les buffers d'image
    displayConfig.imageData = nullptr;
    displayConfig.imageDataAllocated = false;
    
    // Envoyer les données d'affichage initiales à l'ATmega après un court délai
    // (fait dans setup() après initI2C pour s'assurer que la communication est prête)
    for (int i = 0; i < 8; i++) {
        displayConfig.gifFrames[i] = nullptr;
    }
    displayConfig.imageChunkIndex = 0;
    
    DEBUG_PRINTLN("[DISPLAY] Display configuration initialized");
}

void handleDisplayConfig(JsonObject& displayObj) {
    if (displayObj.containsKey("brightness")) {
        displayConfig.brightness = displayObj["brightness"].as<uint8_t>();
        preferences.putUChar(PREF_DISPLAY_BRIGHTNESS, displayConfig.brightness);
        DEBUG_PRINTF("[DISPLAY] Brightness set to %d/255\n", displayConfig.brightness);
    }
    
    if (displayObj.containsKey("mode")) {
        displayConfig.mode = displayObj["mode"].as<String>();
        preferences.putString(PREF_DISPLAY_MODE, displayConfig.mode);
        DEBUG_PRINTF("[DISPLAY] Mode set to: %s\n", displayConfig.mode.c_str());
    }
    
    if (displayObj.containsKey("customData")) {
        JsonObject customData = displayObj["customData"].as<JsonObject>();
        if (customData.containsKey("showProfile")) {
            displayConfig.customData.showProfile = customData["showProfile"].as<bool>();
        }
        if (customData.containsKey("showBattery")) {
            displayConfig.customData.showBattery = customData["showBattery"].as<bool>();
        }
        if (customData.containsKey("showMode")) {
            displayConfig.customData.showMode = customData["showMode"].as<bool>();
        }
        if (customData.containsKey("showKeys")) {
            displayConfig.customData.showKeys = customData["showKeys"].as<bool>();
        }
        if (customData.containsKey("showBacklight")) {
            displayConfig.customData.showBacklight = customData["showBacklight"].as<bool>();
        }
        if (customData.containsKey("showCustom1")) {
            displayConfig.customData.showCustom1 = customData["showCustom1"].as<bool>();
        }
        if (customData.containsKey("showCustom2")) {
            displayConfig.customData.showCustom2 = customData["showCustom2"].as<bool>();
        }
        if (customData.containsKey("customLine1")) {
            displayConfig.customData.customLine1 = customData["customLine1"].as<String>();
        }
        if (customData.containsKey("customLine2")) {
            displayConfig.customData.customLine2 = customData["customLine2"].as<String>();
        }
    }
    
    // Mettre à jour l'écran via I2C vers l'ATmega
    sendDisplayDataToAtmega();
}

// Gérer la configuration des paramètres (debug, logging)
void handleSettingsConfig(JsonObject& settingsObj) {
    DEBUG_PRINTLN("[SETTINGS] Processing settings configuration...");
    
    // Variables pour stocker les nouveaux paramètres
    bool debugEsp32Enabled = false;
    String esp32LogLevel = "info";
    bool loggingEsp32Enabled = false;
    bool atmegaEnabled = false;
    String atmegaLogLevel = "info";
    bool loggingAtmegaEnabled = false;
    bool debugHid = false;
    bool debugI2c = false;
    bool debugWeb = false;
    bool debugDisplay = false;
    bool debugConfig = false;
    
    // Parser les paramètres de debug
    if (settingsObj.containsKey("debug")) {
        JsonObject debugObj = settingsObj["debug"].as<JsonObject>();
        
        if (debugObj.containsKey("esp32Enabled")) {
            debugEsp32Enabled = debugObj["esp32Enabled"].as<bool>();
            preferences.putBool("debug_esp32", debugEsp32Enabled);
            DEBUG_PRINTF("[SETTINGS] ESP32 Debug: %s\n", debugEsp32Enabled ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("esp32LogLevel")) {
            esp32LogLevel = debugObj["esp32LogLevel"].as<String>();
            preferences.putString("esp32_log_level", esp32LogLevel);
            DEBUG_PRINTF("[SETTINGS] ESP32 Log Level: %s\n", esp32LogLevel.c_str());
        }
        
        if (debugObj.containsKey("atmegaEnabled")) {
            atmegaEnabled = debugObj["atmegaEnabled"].as<bool>();
            // Envoyer la commande à l'ATmega pour activer/désactiver le debug
            sendAtmegaDebugCommand(atmegaEnabled);
            preferences.putBool("debug_atmega", atmegaEnabled);
            DEBUG_PRINTF("[SETTINGS] ATmega Debug: %s\n", atmegaEnabled ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("atmegaLogLevel")) {
            atmegaLogLevel = debugObj["atmegaLogLevel"].as<String>();
            // Envoyer le niveau de log à l'ATmega
            sendAtmegaLogLevelCommand(atmegaLogLevel);
            preferences.putString("atmega_log_level", atmegaLogLevel);
            DEBUG_PRINTF("[SETTINGS] ATmega Log Level: %s\n", atmegaLogLevel.c_str());
        }
        
        if (debugObj.containsKey("hid")) {
            debugHid = debugObj["hid"].as<bool>();
            preferences.putBool("debug_hid", debugHid);
            DEBUG_PRINTF("[SETTINGS] HID Debug: %s\n", debugHid ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("i2c")) {
            debugI2c = debugObj["i2c"].as<bool>();
            preferences.putBool("debug_i2c", debugI2c);
            DEBUG_PRINTF("[SETTINGS] I2C Debug: %s\n", debugI2c ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("web")) {
            debugWeb = debugObj["web"].as<bool>();
            preferences.putBool("debug_web", debugWeb);
            DEBUG_PRINTF("[SETTINGS] Web Debug: %s\n", debugWeb ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("display")) {
            debugDisplay = debugObj["display"].as<bool>();
            preferences.putBool("debug_display", debugDisplay);
            DEBUG_PRINTF("[SETTINGS] Display Debug: %s\n", debugDisplay ? "enabled" : "disabled");
        }
        
        if (debugObj.containsKey("config")) {
            debugConfig = debugObj["config"].as<bool>();
            preferences.putBool("debug_config", debugConfig);
            DEBUG_PRINTF("[SETTINGS] Config Debug: %s\n", debugConfig ? "enabled" : "disabled");
        }
    }
    
    // Parser les paramètres de logging
    if (settingsObj.containsKey("logging")) {
        JsonObject loggingObj = settingsObj["logging"].as<JsonObject>();
        
        if (loggingObj.containsKey("esp32Enabled")) {
            loggingEsp32Enabled = loggingObj["esp32Enabled"].as<bool>();
            preferences.putBool("logging_esp32", loggingEsp32Enabled);
            DEBUG_PRINTF("[SETTINGS] ESP32 Logging: %s\n", loggingEsp32Enabled ? "enabled" : "disabled");
        }
        
        if (loggingObj.containsKey("atmegaEnabled")) {
            loggingAtmegaEnabled = loggingObj["atmegaEnabled"].as<bool>();
            // Envoyer la commande à l'ATmega pour activer/désactiver le logging
            sendAtmegaDebugCommand(loggingAtmegaEnabled);  // Utilise la même commande que debug
            preferences.putBool("logging_atmega", loggingAtmegaEnabled);
            DEBUG_PRINTF("[SETTINGS] ATmega Logging: %s\n", loggingAtmegaEnabled ? "enabled" : "disabled");
        }
    }
    
    DEBUG_PRINTLN("[SETTINGS] Settings configuration completed");
}

// Envoyer la commande de debug à l'ATmega
void sendAtmegaDebugCommand(bool enabled) {
    SerialAtmega.write(CMD_SET_ATMEGA_DEBUG);
    SerialAtmega.write(enabled ? 1 : 0);
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    DEBUG_PRINTF("[SETTINGS] ATmega debug command sent: %s\n", enabled ? "enabled" : "disabled");
}

// Envoyer le niveau de log à l'ATmega
void sendAtmegaLogLevelCommand(String level) {
    SerialAtmega.write(CMD_SET_ATMEGA_LOG_LEVEL);
    
    // Convertir le niveau de log en byte
    uint8_t levelByte = 0;  // none
    if (level == "error") levelByte = 1;
    else if (level == "info") levelByte = 2;
    else if (level == "debug") levelByte = 3;
    
    SerialAtmega.write(levelByte);
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    DEBUG_PRINTF("[SETTINGS] ATmega log level command sent: %s (%d)\n", level.c_str(), levelByte);
}

// Fonction simple pour décoder base64
int base64_dec_len(const char* input, int len) {
    int padding = 0;
    if (len > 0 && input[len - 1] == '=') padding++;
    if (len > 1 && input[len - 2] == '=') padding++;
    return (len * 3) / 4 - padding;
}

int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 0;
    return -1;
}

void base64_decode(uint8_t* output, const char* input, int len) {
    int i = 0, j = 0;
    while (i < len) {
        int a = base64_decode_char(input[i++]);
        int b = base64_decode_char(input[i++]);
        int c = base64_decode_char(input[i++]);
        int d = base64_decode_char(input[i++]);
        
        int combined = (a << 18) | (b << 12) | (c << 6) | d;
        
        if (j < len * 3 / 4) output[j++] = (combined >> 16) & 0xFF;
        if (j < len * 3 / 4) output[j++] = (combined >> 8) & 0xFF;
        if (j < len * 3 / 4) output[j++] = combined & 0xFF;
    }
}

void handleDisplayImage(JsonObject& imageObj) {
    if (imageObj.containsKey("frame") && imageObj.containsKey("total") && imageObj.containsKey("data")) {
        uint8_t frame = imageObj["frame"].as<uint8_t>();
        uint8_t total = imageObj["total"].as<uint8_t>();
        String imageDataStr = imageObj["data"].as<String>();
        
        DEBUG_PRINTF("[DISPLAY] Receiving image frame %d/%d, data length: %d\n", frame, total, imageDataStr.length());
        
        // Décoder le base64
        int decodedLength = base64_dec_len((char*)imageDataStr.c_str(), imageDataStr.length());
        if (decodedLength > 0 && decodedLength <= ST7789_IMAGE_SIZE) {
            uint8_t* decodedData = (uint8_t*)malloc(decodedLength);
            if (decodedData) {
                base64_decode((char*)decodedData, (char*)imageDataStr.c_str(), imageDataStr.length());
                
                // Allouer le buffer si nécessaire
                if (total == 1) {
                    // Image unique
                    if (!displayConfig.imageDataAllocated) {
                        displayConfig.imageData = (uint8_t*)malloc(ST7789_IMAGE_SIZE);
                        if (displayConfig.imageData) {
                            displayConfig.imageDataAllocated = true;
                            DEBUG_PRINTLN("[DISPLAY] Image buffer allocated");
                        }
                    }
                    if (displayConfig.imageData) {
                        memcpy(displayConfig.imageData, decodedData, decodedLength);
                        displayConfig.mode = "image";
                        displayConfig.gifFrameCount = 0;
                        DEBUG_PRINTLN("[DISPLAY] Image data stored, sending to ATmega...");
                        sendImageToAtmega(displayConfig.imageData, ST7789_IMAGE_SIZE);
                    }
                } else {
                    // GIF - plusieurs frames
                    if (frame < 8 && total <= 8) {
                        if (!displayConfig.gifFrames[frame]) {
                            displayConfig.gifFrames[frame] = (uint8_t*)malloc(ST7789_IMAGE_SIZE);
                        }
                        if (displayConfig.gifFrames[frame]) {
                            memcpy(displayConfig.gifFrames[frame], decodedData, decodedLength);
                            displayConfig.mode = "gif";
                            displayConfig.gifFrameCount = total;
                            DEBUG_PRINTF("[DISPLAY] GIF frame %d stored\n", frame);
                            
                            // Envoyer la frame à l'ATmega
                            sendImageToAtmega(displayConfig.gifFrames[frame], ST7789_IMAGE_SIZE);
                        }
                    }
                }
                
                free(decodedData);
            } else {
                DEBUG_PRINTLN("[DISPLAY] ERROR: Failed to allocate memory for decoded image");
            }
        } else {
            DEBUG_PRINTF("[DISPLAY] ERROR: Invalid decoded length: %d\n", decodedLength);
        }
        
        // Mettre à jour l'écran via I2C
        sendDisplayDataToAtmega();
    }
}

// Fonction pour envoyer une image RGB565 à l'ATmega par chunks
void sendImageToAtmega(uint8_t* imageData, uint16_t imageSize) {
    DEBUG_PRINTF("[DISPLAY] Sending image to ATmega, size: %d bytes\n", imageSize);
    
    // Envoyer la commande pour commencer la transmission d'image
    // Format: [CMD] [size_low] [size_high] [\n]
    SerialAtmega.write(CMD_SET_DISPLAY_IMAGE);
    SerialAtmega.write((uint8_t)(imageSize & 0xFF));  // Low byte
    SerialAtmega.write((uint8_t)((imageSize >> 8) & 0xFF));  // High byte
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    delay(10);  // Petit délai pour que l'ATmega soit prêt
    
    // Envoyer l'image par chunks
    // Format pour chaque chunk: [CMD] [chunk_idx_low] [chunk_idx_high] [chunk_size] [data...] [\n]
    uint16_t chunkIndex = 0;
    uint16_t remaining = imageSize;
    
    while (remaining > 0) {
        uint8_t chunkSize = (remaining > IMAGE_CHUNK_SIZE) ? IMAGE_CHUNK_SIZE : remaining;
        
        SerialAtmega.write(CMD_SET_DISPLAY_IMAGE_CHUNK);
        SerialAtmega.write((uint8_t)(chunkIndex & 0xFF));  // Low byte du chunk index
        SerialAtmega.write((uint8_t)((chunkIndex >> 8) & 0xFF));  // High byte du chunk index
        SerialAtmega.write(chunkSize);
        SerialAtmega.write(imageData + (chunkIndex * IMAGE_CHUNK_SIZE), chunkSize);
        SerialAtmega.write('\n');
        SerialAtmega.flush();
        
        chunkIndex++;
        remaining -= chunkSize;
        
        // Petit délai entre les chunks pour éviter de surcharger UART
        delay(2);
        
        // Afficher la progression tous les 100 chunks
        if (chunkIndex % 100 == 0) {
            DEBUG_PRINTF("[DISPLAY] Sent %d chunks, remaining: %d bytes\n", chunkIndex, remaining);
        }
    }
    
    DEBUG_PRINTF("[DISPLAY] Image transmission complete, sent %d chunks\n", chunkIndex);
}

void updateDisplay() {
    // Envoyer les données d'affichage à l'ATmega via UART
    sendDisplayDataToAtmega();
}

void sendDisplayDataToAtmega() {
    // Envoyer la configuration de l'écran à l'ATmega via UART
    // Format: [CMD] [brightness] [mode_len] [mode] [profile_name_len] [profile_name] [output_mode_len] [output_mode] [keys_count] [backlight_enabled] [backlight_brightness] [custom1_len] [custom1] [custom2_len] [custom2] [\n]
    
    SerialAtmega.write(CMD_SET_DISPLAY_DATA);
    
    // Brightness
    SerialAtmega.write(displayConfig.brightness);
    
    // Mode (data/image/gif)
    String mode = displayConfig.mode;
    SerialAtmega.write(mode.length());
    SerialAtmega.write((uint8_t*)mode.c_str(), mode.length());
    
    // Nom du profil actif
    String profileName = "";
    if (config.profileCount > 0 && config.activeProfileIndex < config.profileCount) {
        profileName = config.profiles[config.activeProfileIndex].name;
    }
    SerialAtmega.write(profileName.length());
    if (profileName.length() > 0) {
        SerialAtmega.write((uint8_t*)profileName.c_str(), profileName.length());
    }
    
    // Mode de sortie (usb/bluetooth)
    String outputMode = config.outputMode;
    SerialAtmega.write(outputMode.length());
    SerialAtmega.write((uint8_t*)outputMode.c_str(), outputMode.length());
    
    // Nombre de touches configurées
    int keysCount = 0;
    if (config.activeProfileIndex < config.profileCount) {
        Profile& profile = config.profiles[config.activeProfileIndex];
        for (int i = 0; i < TOTAL_KEYS; i++) {
            if (profile.keys[i].type.length() > 0) {
                keysCount++;
            }
        }
    }
    SerialAtmega.write((uint8_t)keysCount);
    
    // État du backlight
    SerialAtmega.write(backlightEnabled ? 1 : 0);
    SerialAtmega.write(backlightBrightness);
    
    // Lignes personnalisées
    String custom1 = displayConfig.customData.customLine1;
    SerialAtmega.write(custom1.length());
    if (custom1.length() > 0) {
        SerialAtmega.write((uint8_t*)custom1.c_str(), custom1.length());
    }
    
    String custom2 = displayConfig.customData.customLine2;
    SerialAtmega.write(custom2.length());
    if (custom2.length() > 0) {
        SerialAtmega.write((uint8_t*)custom2.c_str(), custom2.length());
    }
    
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    
    DEBUG_PRINTLN("[DISPLAY] Display data sent to ATmega successfully");
    
    // Demander la mise à jour de l'écran
    delay(10);
    SerialAtmega.write(CMD_UPDATE_DISPLAY);
    SerialAtmega.write('\n');
    SerialAtmega.flush();
}
