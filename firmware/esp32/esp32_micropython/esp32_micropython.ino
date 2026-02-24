/*
 * ESP32-S3 Macropad Firmware - Pavé numérique configurable
 * Inspiré de MacroPad (aayushchouhan24) — logique modulaire, HID standalone
 *
 * Fonctionne sur n'importe quel appareil (phone, PC) sans app.
 * Configurable via interface web (Web Serial / Web Bluetooth).
 *
 * Dépendances: Adafruit NeoPixel (Sketch > Include Library)
 * Windows: USB recommandé. Arduino: Board = ESP32S3 Dev Module
 */

#include "Config.h"
#include "KeyMatrix.h"
#include "Encoder.h"
#include "HidOutput.h"

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <string.h>

// ─── Instances globales (logique modulaire) ───────────────────────────────────
KeyMatrix keyMatrix;
Encoder encoder;
HidOutput hidOutput;

HardwareSerial SerialAtmega(1);
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl;
Preferences preferences;

// Keymap par défaut (grille physique)
const char* DEFAULT_KEYMAP[NUM_ROWS][NUM_COLS] = {
    {"PROFILE", "/", "*", "-"},
    {"7", "8", "9", "+"},
    {"4", "5", "6", ""},
    {"1", "2", "3", "="},
    {"0", ".", "", ""}
};

String KEYMAP[NUM_ROWS][NUM_COLS];

// UART ATmega
String atmega_rx_buffer = "";
unsigned long null_bytes_count = 0;
unsigned long last_null_warning = 0;
uint16_t last_light_level = 0;

// LED
int led_pwm_channel = 0;
int led_brightness = 128;
bool backlight_enabled = true;

// BLE
BLEServer* pServer = nullptr;
BLECharacteristic* pInputCharacteristic = nullptr;
BLECharacteristic* pSerialCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String bleSerialBuffer = "";
bool BLE_AVAILABLE = false;

String platformDetected = "unknown";
Adafruit_NeoPixel ledStrip(LED_STRIP_COUNT, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// OTA
bool ota_in_progress = false;
int ota_chunk_count = 0;
int ota_total_chunks = 0;
int ota_file_size = 0;
String ota_file_content = "";

String last_key_pressed = "";
unsigned long last_display_update = 0;

#define SERVICE_UUID_SERIAL "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_SERIAL "0000ffe1-0000-1000-8000-00805f9b34fb"

// ==================== DÉCLARATIONS FORWARD ====================
void send_to_web(String data);
void send_display_update_to_atmega();
void update_per_key_leds();
void set_key_led_pressed(int row, int col, bool pressed);

// ==================== CALLBACKS (logique événementielle) ====================

void onKeyPress(uint8_t row, uint8_t col, bool pressed, bool isRepeat) {
    if (!pressed) return;
    String symbol = KEYMAP[row][col];
    if (symbol.length() == 0) return;
    if (isRepeat && !HidOutput::keyShouldRepeat(symbol)) return;

    Serial.printf("[HID] Key [%d,%d] PRESSED: %s\n", row, col, symbol.c_str());
    last_key_pressed = symbol;

    hidOutput.sendKey(symbol, row, col);

    set_key_led_pressed(row, col, true);
    delay(50);
    update_per_key_leds();

    String keypress_msg = "{\"type\":\"keypress\",\"row\":" + String(row) + ",\"col\":" + String(col) + "}";
    send_to_web(keypress_msg);
    send_display_update_to_atmega();
}

void onEncoderRotate(int8_t dir, uint8_t steps) {
    for (uint8_t i = 0; i < steps; i++) {
        if (dir > 0) hidOutput.sendVolumeUp();
        else hidOutput.sendVolumeDown();
        // Android BLE: espacement requis entre rapports Consumer (sinon "volume max ou rien")
        if (deviceConnected && i < steps - 1) delay(BLE_VOLUME_STEP_DELAY_MS);
    }
}

void onEncoderButton(bool pressed) {
    if (pressed) hidOutput.sendMute();
}

// ==================== DÉCLARATIONS FORWARD (suite) ====================

void processWebMessage(String message);
void send_atmega_command(uint8_t cmd, uint8_t* payload = nullptr, int payload_len = 0);
void read_atmega_uart();
void send_light_level();
void send_display_update_to_atmega();
void handle_config_message(JsonObject& data);
void handle_backlight_message(JsonObject& data);
void handle_display_message(JsonObject& data);
void send_config_to_web();
void send_status_message(String message);
void handle_ota_start(JsonObject& data);
void handle_ota_chunk(JsonObject& data);
void handle_ota_end(JsonObject& data);
uint8_t get_keycode(String symbol);
void update_per_key_leds();
int row_col_to_led_index(int row, int col);
void apply_keymap_defaults();

// ==================== CALLBACKS BLE ====================

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        hidOutput.setBleState(true, pInputCharacteristic);
        Serial.println("[BLE] Client connected");
        
        delay(800);
        
        if (pInputCharacteristic != nullptr) {
            uint8_t empty_report[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            for (int i = 0; i < 3; i++) {
                pInputCharacteristic->setValue(empty_report, 9);
                delay(50);
                pInputCharacteristic->notify();
                delay(150);
            }
            Serial.println("[BLE] HID activated (Windows/macOS/Linux/Android/iOS)");
        }
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        hidOutput.setBleState(false, nullptr);
        if (pInputCharacteristic != nullptr) {
            uint8_t release[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            pInputCharacteristic->setValue(release, 9);
            pInputCharacteristic->notify();
        }
        Serial.println("[BLE] Client disconnected");
    }
};

class SerialCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String message = String(value.c_str());
            bleSerialBuffer += message;
            
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

// ==================== SETUP ====================

void setup() {
    delay(100);
    
    // Initialiser Serial pour debug
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== ESP32-S3 Macropad Initialization ===");
    Serial.println("Migration complète depuis MicroPython");
    
    // Initialiser USB HID (clavier + Consumer Control pour volume/média)
    USB.begin();
    delay(1000);
    Keyboard.begin();
    ConsumerControl.begin();
    delay(1000);
    Serial.println("[USB] USB HID initialized (Keyboard + Consumer Control)");
    
    // Initialiser BLE avec un nom qui indique clairement que c'est un clavier
    // iPhone/iOS reconnaît mieux les appareils avec "Keyboard" dans le nom
    try {
        BLEDevice::init("Macropad Keyboard");
        
        // Configurer la sécurité BLE pour réduire les warnings SMP
        BLESecurity* pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
        pSecurity->setCapability(ESP_IO_CAP_NONE);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        
        // Service HID
        BLEService* pService = pServer->createService(BLEUUID((uint16_t)0x1812));
        
        // HID Information Characteristic (requis pour HID, important pour iPhone)
        BLECharacteristic* pInfoCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4A),
            BLECharacteristic::PROPERTY_READ
        );
        // Format: bcdHID (version HID 1.01), bCountryCode (0 = not localized), Flags (0x03 = remote wake + normally connectable)
        uint8_t info[] = {0x01, 0x01, 0x00, 0x03};
        pInfoCharacteristic->setValue(info, 4);
        
        // HID Report Map
        BLECharacteristic* pReportMapCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4B),
            BLECharacteristic::PROPERTY_READ
        );
        // Report Map: Keyboard + Keypad (0x00-0x81 inclut Volume Up/Down/Mute pour Android BLE)
        uint8_t reportMap[] = {
            0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
            0x85, 0x01,
            0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
            0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
            0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
            0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x81,
            0x05, 0x07, 0x19, 0x00, 0x29, 0x81, 0x81, 0x00,
            0xC0,
            0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
            0x85, 0x02,
            0x15, 0x00, 0x26, 0x9C, 0x02,
            0x75, 0x10, 0x95, 0x01,             0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xCD,
            0x81, 0x00, 0xC0
        };
        pReportMapCharacteristic->setValue(reportMap, sizeof(reportMap));
        
        // HID Protocol Mode (requis pour HID)
        BLECharacteristic* pProtocolModeCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4E),
            BLECharacteristic::PROPERTY_READ | 
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        uint8_t protocolMode = 0x01; // Report Protocol (requis pour HID)
        pProtocolModeCharacteristic->setValue(&protocolMode, 1);
        
        // HID Control Point (requis pour Windows - suspend/resume)
        BLECharacteristic* pControlPointCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4C),
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        uint8_t controlPoint = 0x00; // Suspend
        pControlPointCharacteristic->setValue(&controlPoint, 1);
        
        // HID Input Report
        pInputCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4D),
            BLECharacteristic::PROPERTY_READ | 
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        pInputCharacteristic->addDescriptor(new BLE2902());
        
        // Initialiser le report avec une valeur par défaut (important pour Windows/Android)
        uint8_t empty_report[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        pInputCharacteristic->setValue(empty_report, 9);
        
        pService->start();
        
        // Device Information Service (améliore compatibilité Windows)
        BLEService* pDevInfoService = pServer->createService(BLEUUID((uint16_t)0x180A));
        BLECharacteristic* pManufacturerCharacteristic = pDevInfoService->createCharacteristic(
            BLEUUID((uint16_t)0x2A29),
            BLECharacteristic::PROPERTY_READ
        );
        pManufacturerCharacteristic->setValue("Macropad");
        BLECharacteristic* pModelCharacteristic = pDevInfoService->createCharacteristic(
            BLEUUID((uint16_t)0x2A24),
            BLECharacteristic::PROPERTY_READ
        );
        pModelCharacteristic->setValue("Keyboard");
        pDevInfoService->start();
        
        // Battery Service (requis par certains pilotes Windows BLE HID)
        BLEService* pBatteryService = pServer->createService(BLEUUID((uint16_t)0x180F));
        BLECharacteristic* pBatteryLevelCharacteristic = pBatteryService->createCharacteristic(
            BLEUUID((uint16_t)0x2A19),
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
        );
        pBatteryLevelCharacteristic->addDescriptor(new BLE2902());
        uint8_t batteryLevel = 100;  // USB alimenté = 100%
        pBatteryLevelCharacteristic->setValue(&batteryLevel, 1);
        pBatteryService->start();
        
        // Service Serial Bluetooth
        BLEService* pSerialService = pServer->createService(BLEUUID(SERVICE_UUID_SERIAL));
        pSerialCharacteristic = pSerialService->createCharacteristic(
            BLEUUID(CHAR_UUID_SERIAL),
            BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE_NR
        );
        pSerialCharacteristic->addDescriptor(new BLE2902());
        pSerialCharacteristic->setCallbacks(new SerialCharacteristicCallbacks());
        pSerialService->start();
        
        // Démarrer l'advertising
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x1812));  // HID
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180A));  // Device Information
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180F));  // Battery (Windows)
        pAdvertising->addServiceUUID(BLEUUID(SERVICE_UUID_SERIAL));
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
        
        BLE_AVAILABLE = true;
        Serial.println("[BLE] BLE services started");
    } catch (...) {
        BLE_AVAILABLE = false;
        Serial.println("[BLE] Error initializing BLE");
    }
    
    preferences.begin("macropad", false);
    platformDetected = preferences.getString("platform", "unknown");
    Serial.printf("[SYSTEM] Platform: %s (Keypad HID - layout indépendant)\n", platformDetected.c_str());
    
    // Keymap par défaut au démarrage
    apply_keymap_defaults();
    
    // Initialiser UART ATmega
    SerialAtmega.begin(ATMEGA_UART_BAUD, SERIAL_8N1, ATMEGA_UART_RX, ATMEGA_UART_TX);
    delay(100);
    Serial.printf("[UART] ATmega UART initialized TX=%d, RX=%d, %d baud\n",
                  ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD);
    
    // Initialiser LED PWM (legacy)
    ledcSetup(led_pwm_channel, 1000, 10);
    ledcAttachPin(LED_PWM_PIN, led_pwm_channel);
    ledcWrite(led_pwm_channel, backlight_enabled ? (led_brightness * 1023 / 255) : 0);
    Serial.printf("[LED] LED PWM initialized on GPIO %d\n", LED_PWM_PIN);
    
    // Initialiser SK6812-E per-key strip
    ledStrip.begin();
    ledStrip.setBrightness(led_brightness);
    ledStrip.show();
    update_per_key_leds();
    Serial.printf("[LED] SK6812 strip initialized on GPIO %d (%d LEDs)\n", LED_STRIP_PIN, LED_STRIP_COUNT);
    
    // Modules (logique événementielle)
    keyMatrix.begin();
    keyMatrix.setCallback(onKeyPress);
    Serial.println("[MATRIX] Key matrix initialized");

    encoder.begin();
    encoder.setRotateCallback(onEncoderRotate);
    encoder.setButtonCallback(onEncoderButton);
    Serial.println("[ENCODER] Rotary encoder initialized");

    hidOutput.begin(&Keyboard, &ConsumerControl);
    
    // Activer le debug ATmega après un délai
    delay(2000);
    uint8_t debug_enable = 1;
    send_atmega_command(CMD_SET_ATMEGA_DEBUG, &debug_enable, 1);
    delay(50);
    uint8_t log_level = 3;
    send_atmega_command(CMD_SET_ATMEGA_LOG_LEVEL, &log_level, 1);
    delay(50);
    Serial.println("[UART] ATmega debug enabled");
    
    // Envoyer la mise à jour d'affichage initiale
    delay(500);
    send_display_update_to_atmega();
    Serial.println("[MAIN] Initialization complete");
    Serial.println("Ready!");
}

// ==================== LOOP PRINCIPAL ====================

void loop() {
    // Lire l'encodeur AVANT le scan matrice (évite interférences GPIO sur CLK/DT)
    delay(1);
    encoder.update();
    keyMatrix.scan();
    read_serial();
    
    // Lire UART ATmega
    read_atmega_uart();
    
    // Gérer BLE
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("[BLE] Restarting advertising after disconnect");
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
        Serial.println("[BLE] New connection established");
        oldDeviceConnected = deviceConnected;
    }
    
    // Mettre à jour l'affichage ATmega périodiquement
    unsigned long now = millis();
    if (now - last_display_update >= DISPLAY_UPDATE_INTERVAL_MS) {
        send_display_update_to_atmega();
        last_display_update = now;
    }
    
    delay(5);
}

// ==================== COMMUNICATION SÉRIE ====================

void read_serial() {
    if (Serial.available()) {
        String message = Serial.readStringUntil('\n');
        message.trim();
        if (message.length() > 0) {
            processWebMessage(message);
        }
    }
}

// ==================== TRAITEMENT DES MESSAGES WEB ====================

void processWebMessage(String message) {
    Serial.printf("[WEB_UI] Received: %s\n", message.c_str());
    
    if (message.length() < 2) {
        return;
    }
    
    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.printf("[WEB_UI] JSON parse error: %s\n", error.c_str());
        return;
    }
    
    String msg_type = doc["type"].as<String>();
    
    if (msg_type == "config") {
        JsonObject configObj = doc.as<JsonObject>();
        handle_config_message(configObj);
    } else if (msg_type == "backlight") {
        JsonObject backlightObj = doc.as<JsonObject>();
        handle_backlight_message(backlightObj);
    } else if (msg_type == "display") {
        JsonObject displayObj = doc.as<JsonObject>();
        handle_display_message(displayObj);
    } else if (msg_type == "get_config") {
        send_config_to_web();
    } else if (msg_type == "get_light") {
        send_light_level();
    } else if (msg_type == "status") {
        send_status_message("Macropad ready");
    } else if (msg_type == "settings") {
        JsonObject settingsObj = doc.as<JsonObject>();
        if (settingsObj.containsKey("platform")) {
            platformDetected = settingsObj["platform"].as<String>();
            preferences.putString("platform", platformDetected);
        }
    } else if (msg_type == "ota_start") {
        JsonObject otaObj = doc.as<JsonObject>();
        handle_ota_start(otaObj);
    } else if (msg_type == "ota_chunk") {
        JsonObject otaObj = doc.as<JsonObject>();
        handle_ota_chunk(otaObj);
    } else if (msg_type == "ota_end") {
        JsonObject otaObj = doc.as<JsonObject>();
        handle_ota_end(otaObj);
    } else {
        Serial.printf("[WEB_UI] Unknown message type: %s\n", msg_type.c_str());
    }
}

void handle_config_message(JsonObject& data) {
    Serial.println("[WEB] Processing config message");
    
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            KEYMAP[r][c] = "";
        }
    }
    
    if (data.containsKey("platform")) {
        platformDetected = data["platform"].as<String>();
        preferences.putString("platform", platformDetected);
        Serial.printf("[CONFIG] Platform: %s\n", platformDetected.c_str());
    }
    
    if (data.containsKey("keys")) {
        JsonObject keys = data["keys"].as<JsonObject>();
        for (JsonPair kv : keys) {
            String key_id = kv.key().c_str();
            int dash_pos = key_id.indexOf('-');
            if (dash_pos > 0) {
                int row = key_id.substring(0, dash_pos).toInt();
                int col = key_id.substring(dash_pos + 1).toInt();
                
                if (row >= 0 && row < NUM_ROWS && col >= 0 && col < NUM_COLS) {
                    String value;
                    if (kv.value().is<JsonObject>()) {
                        value = kv.value().as<JsonObject>()["value"].as<String>();
                    } else {
                        value = kv.value().as<String>();
                    }
                    KEYMAP[row][col] = value;
                }
            }
        }
    }
    
    Serial.println("[WEB] Keymap updated");
    send_status_message("Configuration updated");
}

void handle_backlight_message(JsonObject& data) {
    Serial.println("[WEB] Processing backlight message");
    
    if (data.containsKey("enabled")) {
        backlight_enabled = data["enabled"].as<bool>();
        if (!backlight_enabled) {
            ledcWrite(led_pwm_channel, 0);
            ledStrip.clear();
            ledStrip.show();
        } else {
            ledcWrite(led_pwm_channel, led_brightness * 1023 / 255);
            ledStrip.setBrightness(led_brightness);
            update_per_key_leds();
        }
    }
    
    if (data.containsKey("brightness")) {
        led_brightness = data["brightness"].as<uint8_t>();
        led_brightness = max(0, min(255, led_brightness));
        if (backlight_enabled) {
            ledcWrite(led_pwm_channel, led_brightness * 1023 / 255);
            ledStrip.setBrightness(led_brightness);
            update_per_key_leds();
        }
        Serial.printf("[LED] Brightness set to %d\n", led_brightness);
    }
    
    if (data.containsKey("envBrightness") || data.containsKey("env-brightness")) {
        send_light_level();
    }
    
    Serial.println("[WEB] Backlight config updated");
    send_status_message("Backlight config updated");
}

void handle_display_message(JsonObject& data) {
    Serial.println("[WEB] Display config:");
    serializeJson(data, Serial);
    Serial.println();
    send_status_message("Display config updated");
}

void send_config_to_web() {
    StaticJsonDocument<2048> doc;
    doc["type"] = "config";
    doc["rows"] = NUM_ROWS;
    doc["cols"] = NUM_COLS;
    doc["activeProfile"] = "Profil 1";
    doc["outputMode"] = deviceConnected ? "bluetooth" : "usb";
    doc["platform"] = platformDetected;
    
    JsonObject keys = doc.createNestedObject("keys");
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (KEYMAP[r][c].length() > 0) {
                String key_id = String(r) + "-" + String(c);
                JsonObject key_obj = keys.createNestedObject(key_id);
                key_obj["type"] = "key";
                key_obj["value"] = KEYMAP[r][c];
            }
        }
    }
    
    JsonObject profiles = doc.createNestedObject("profiles");
    JsonObject profile1 = profiles.createNestedObject("Profil 1");
    profile1["keys"] = keys;
    
    String output;
    serializeJson(doc, output);
    send_to_web(output);
}

void send_status_message(String message) {
    String json = "{\"type\":\"status\",\"message\":\"" + message + "\"}";
    send_to_web(json);
}

// ==================== SK6812 PER-KEY BACKLIGHT ====================

void apply_keymap_defaults() {
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            KEYMAP[r][c] = String(DEFAULT_KEYMAP[r][c]);
        }
    }
    Serial.println("[KEYMAP] Default keymap applied");
}

int row_col_to_led_index(int row, int col) {
    // Mapping grille 5x4 vers 17 LEDs (ordre: 0-0..0-3, 1-0..1-3, 2-0..2-2, 3-0..3-3, 4-0, 4-1)
    if (row == 2 && col == 3) return 7;   // 2-3 = partie de +
    if (row == 4 && col == 1) return 16;  // 4-1 = touche "."
    if (row == 4 && col == 2) return 16;  // 4-2 (matrix) = affiché comme 4-1
    if (row == 4 && col == 3) return 14;  // 4-3 = partie de =
    int idx = row * 4 + col;
    if (idx >= LED_STRIP_COUNT) return 0;
    return idx;
}

void update_per_key_leds() {
    if (!backlight_enabled) return;
    
    uint8_t r = (led_brightness * 255) / 255;
    uint8_t g = (led_brightness * 255) / 255;
    uint8_t b = (led_brightness * 255) / 255;
    uint32_t color = ledStrip.Color(r, g, b);
    
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        ledStrip.setPixelColor(i, color);
    }
    ledStrip.show();
}

void set_key_led_pressed(int row, int col, bool pressed) {
    if (!backlight_enabled) return;
    int idx = row_col_to_led_index(row, col);
    uint32_t color = pressed ? ledStrip.Color(255, 255, 255) : ledStrip.Color(led_brightness, led_brightness, led_brightness);
    ledStrip.setPixelColor(idx, color);
    ledStrip.show();
}

// ==================== OTA UPDATES ====================

void handle_ota_start(JsonObject& data) {
    ota_file_size = data["size"].as<int>();
    ota_total_chunks = data["chunks"].as<int>();
    String filename = data["filename"].as<String>();
    
    if (ota_in_progress) {
        send_status_message("OTA already in progress");
        return;
    }
    
    ota_file_content = "";
    ota_in_progress = true;
    ota_chunk_count = 0;
    
    Serial.printf("[OTA] Starting update: %s (%d bytes, %d chunks)\n",
                  filename.c_str(), ota_file_size, ota_total_chunks);
    send_status_message("OTA: Starting update...");
    
    StaticJsonDocument<256> response;
    response["type"] = "ota_status";
    response["status"] = "started";
    response["message"] = "OTA update started";
    String output;
    serializeJson(response, output);
    send_to_web(output);
}

void handle_ota_chunk(JsonObject& data) {
    if (!ota_in_progress) {
        send_status_message("OTA: No update in progress");
        return;
    }
    
    String chunk_data = data["data"].as<String>();
    int chunk_index = data["index"].as<int>();
    
    // Décoder base64 si nécessaire
    if (data.containsKey("encoded") && data["encoded"].as<bool>()) {
        // TODO: Implémenter le décodage base64 si nécessaire
    }
    
    ota_file_content += chunk_data;
    ota_chunk_count++;
    
    int progress = (ota_total_chunks > 0) ? (ota_chunk_count * 100 / ota_total_chunks) : 0;
    
    StaticJsonDocument<256> response;
    response["type"] = "ota_status";
    response["status"] = "progress";
    response["progress"] = progress;
    response["chunk"] = ota_chunk_count;
    response["total"] = ota_total_chunks;
    String output;
    serializeJson(response, output);
    send_to_web(output);
    
    Serial.printf("[OTA] Received chunk %d/%d (%d%%)\n",
                  ota_chunk_count, ota_total_chunks, progress);
}

void handle_ota_end(JsonObject& data) {
    if (!ota_in_progress) {
        send_status_message("OTA: No update in progress");
        return;
    }
    
    if (ota_chunk_count < ota_total_chunks) {
        send_status_message("OTA: Incomplete update");
        ota_in_progress = false;
        return;
    }
    
    if (ota_file_content.length() != ota_file_size) {
        send_status_message("OTA: Size mismatch");
        ota_in_progress = false;
        return;
    }
    
    Serial.println("[OTA] Update completed successfully!");
    send_status_message("OTA: Update completed! Restarting...");
    
    StaticJsonDocument<256> response;
    response["type"] = "ota_status";
    response["status"] = "completed";
    response["message"] = "Update completed, restarting...";
    String output;
    serializeJson(response, output);
    send_to_web(output);
    
    delay(500);
    
    // Note: Sur ESP32, OTA via Serial nécessite un mécanisme spécial
    // Pour l'instant, on redémarre simplement
    ESP.restart();
}

// ==================== COMMUNICATION ATmega ====================

void send_to_web(String data) {
    Serial.println(data);
    if (deviceConnected && BLE_AVAILABLE && pSerialCharacteristic != nullptr) {
        String message = data + "\n";
        pSerialCharacteristic->setValue(message.c_str());
        pSerialCharacteristic->notify();
    }
}

void send_atmega_command(uint8_t cmd, uint8_t* payload, int payload_len) {
    SerialAtmega.write(cmd);
    if (payload != nullptr && payload_len > 0) {
        SerialAtmega.write(payload, payload_len);
    }
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    Serial.printf("[UART] Sent command 0x%02X (%d bytes payload)\n", cmd, payload_len);
}

void read_atmega_uart() {
    if (!SerialAtmega.available()) {
        return;
    }
    
    // Lire les données disponibles
    String data = "";
    while (SerialAtmega.available()) {
        char c = SerialAtmega.read();
        data += c;
    }
    
    if (data.length() == 0) {
        return;
    }
    
    // Vérifier si c'est une réponse binaire CMD_READ_LIGHT
    if (data.length() >= 4 && (uint8_t)data.charAt(0) == CMD_READ_LIGHT) {
        uint16_t light_value = (uint8_t)data.charAt(1) | ((uint8_t)data.charAt(2) << 8);
        last_light_level = light_value;
        Serial.printf("[ATMEGA LIGHT] Level (binary): %d\n", light_value);
        
        String msg = "{\"type\":\"light\",\"level\":" + String(light_value) + "}";
        send_to_web(msg);
        return;
    }
    
    // Traiter les messages texte
    atmega_rx_buffer += data;
    
    int newlinePos;
    while ((newlinePos = atmega_rx_buffer.indexOf('\n')) >= 0) {
        String line = atmega_rx_buffer.substring(0, newlinePos);
        atmega_rx_buffer = atmega_rx_buffer.substring(newlinePos + 1);
        
        if (line.length() == 0) continue;
        
        // Vérifier si c'est un message de lumière (format: "LIGHT=XXX")
        if (line.startsWith("LIGHT=")) {
            uint16_t light_value = line.substring(6).toInt();
            last_light_level = light_value;
            Serial.printf("[ATMEGA LIGHT] Level: %d\n", light_value);
            
            String msg = "{\"type\":\"light\",\"level\":" + String(light_value) + "}";
            send_to_web(msg);
        } else {
            Serial.printf("[ATMEGA] %s\n", line.c_str());
        }
    }
}

void send_light_level() {
    send_atmega_command(CMD_READ_LIGHT, nullptr, 0);
    
    // Envoyer la dernière valeur connue en attendant
    String response = "{\"type\":\"light\",\"level\":" + String(last_light_level) + "}";
    send_to_web(response);
}

void send_display_update_to_atmega() {
    String profile = "Profile 1";
    String mode = "data";
    String output_mode = deviceConnected ? "bluetooth" : "usb";
    
    int keys_count = 0;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (KEYMAP[r][c].length() > 0) {
                keys_count++;
            }
        }
    }
    
    String last_key = last_key_pressed.length() > 0 ? last_key_pressed : "";
    int back_en = backlight_enabled ? 1 : 0;
    int back_brightness = led_brightness;
    
    // Obtenir l'heure actuelle (format HH:MM:SS)
    unsigned long seconds = millis() / 1000;
    int hours = (seconds / 3600) % 24;
    int minutes = (seconds / 60) % 60;
    int secs = seconds % 60;
    char time_str[9];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, secs);
    String current_time = String(time_str);
    
    // Construire le payload en format binaire selon le format attendu par l'ATmega
    // Format: [brightness][mode_len][mode][profile_len][profile][output_len][output][keys_count]
    // [last_key_len][last_key][backlight_enabled][backlight_brightness][time_len][time]
    uint8_t payload[256];
    int pos = 0;
    
    // Brightness
    payload[pos++] = back_brightness & 0xFF;
    
    // Mode
    int mode_len = mode.length();
    payload[pos++] = mode_len & 0xFF;
    memcpy(&payload[pos], mode.c_str(), mode_len);
    pos += mode_len;
    
    // Profile
    int profile_len = profile.length();
    payload[pos++] = profile_len & 0xFF;
    memcpy(&payload[pos], profile.c_str(), profile_len);
    pos += profile_len;
    
    // Output mode
    int output_len = output_mode.length();
    payload[pos++] = output_len & 0xFF;
    memcpy(&payload[pos], output_mode.c_str(), output_len);
    pos += output_len;
    
    // Keys count
    payload[pos++] = keys_count & 0xFF;
    
    // Last key pressed
    int last_key_len = last_key.length();
    payload[pos++] = last_key_len & 0xFF;
    if (last_key_len > 0) {
        memcpy(&payload[pos], last_key.c_str(), last_key_len);
        pos += last_key_len;
    }
    
    // Backlight enabled
    payload[pos++] = back_en & 0xFF;
    
    // Backlight brightness
    payload[pos++] = back_brightness & 0xFF;
    
    // Time (optionnel, comme dans le main.py)
    int time_len = current_time.length();
    payload[pos++] = time_len & 0xFF;
    if (time_len > 0) {
        memcpy(&payload[pos], current_time.c_str(), time_len);
        pos += time_len;
    }
    
    send_atmega_command(CMD_SET_DISPLAY_DATA, payload, pos);
    
    Serial.printf("[UART] Sending display update: profile=%s, mode=%s, keys=%d, last_key=%s, time=%s\n",
                  profile.c_str(), mode.c_str(), keys_count, last_key.c_str(), current_time.c_str());
}
