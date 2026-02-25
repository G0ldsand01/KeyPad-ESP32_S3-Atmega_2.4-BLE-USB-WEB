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
bool env_brightness_enabled = false;  // Toggle "Selon l'environnement" du web

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
unsigned long last_light_poll = 0;
unsigned long last_last_key_send = 0;
#define LAST_KEY_SEND_MIN_MS 500   // Throttle: évite double envoi sur un même appui
uint16_t last_light_sent_to_web = 0xFFFF;  // Valeur invalide pour forcer premier envoi
unsigned long last_light_send_time = 0;
#define LIGHT_SEND_MIN_INTERVAL_MS 2000  // Throttle: max 1 envoi / 2 s (sauf si valeur change)
unsigned long last_uart_log_to_web = 0;
#define UART_LOG_TO_WEB_INTERVAL_MS 1000  // Throttle: max 1 uart_log / s vers web (éviter flood BLE)

// BLE Switch: PROFILE+1 maintenu 2s → déconnecte et permet de connecter un autre appareil
unsigned long bleSwitchComboStart = 0;
unsigned long bleSwitchLastTrigger = 0;

#define SERVICE_UUID_SERIAL "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID_SERIAL "0000ffe1-0000-1000-8000-00805f9b34fb"

// ==================== DÉCLARATIONS FORWARD ====================
void send_to_web(String data);
void send_uart_log_to_web(const char* dir, const char* msg);
void send_last_key_to_atmega();
void update_per_key_leds();
void set_key_led_pressed(int row, int col, bool pressed);
void update_builtin_led_from_light();

// ==================== CALLBACKS (logique événementielle) ====================

void onKeyPress(uint8_t row, uint8_t col, bool pressed, bool isRepeat) {
    if (!pressed) return;
    String symbol = KEYMAP[row][col];
    if (symbol.length() == 0) return;
    if (isRepeat && !HidOutput::keyShouldRepeat(symbol)) return;

#if ENABLE_BLE_DEVICE_SWITCH
    // Ne pas envoyer si combo PROFILE+1 en cours (switch BLE)
    if (keyMatrix.isKeyPressed(0, 0) && keyMatrix.isKeyPressed(3, 0)) return;
#endif

    Serial.printf("[HID] Key [%d,%d] PRESSED: %s\n", row, col, symbol.c_str());
    last_key_pressed = symbol;

    hidOutput.sendKey(symbol, row, col);

    set_key_led_pressed(row, col, true);
    delay(50);
    update_per_key_leds();

    String keypress_msg = "{\"type\":\"keypress\",\"row\":" + String(row) + ",\"col\":" + String(col) + "}";
    send_to_web(keypress_msg);
    send_last_key_to_atmega();
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
void send_last_key_to_atmega();
void send_display_data_to_atmega();
void handle_config_message(JsonObject& data);
void handle_backlight_message(JsonObject& data);
void handle_display_message(JsonObject& data);
void send_config_to_web();
uint8_t count_configured_keys();
void send_status_message(String message);
void handle_ota_start(JsonObject& data);
void handle_ota_chunk(JsonObject& data);
void handle_ota_end(JsonObject& data);
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
        send_display_data_to_atmega();
        
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
        send_display_data_to_atmega();
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
    // IMPORTANT: Tools > USB CDC On Boot > Enabled (pour voir le Serial)
    // Délai pour laisser le port USB s'initialiser après le boot
    delay(2000);
    
    Serial.begin(115200);
    delay(500);
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
    // Charger config backlight persistée (env_brightness = LED selon luminosité)
    env_brightness_enabled = preferences.getBool("env_brightness", true);  // true = LED built-in suit la luminosité par défaut
    backlight_enabled = preferences.getBool("backlight_en", true);
    led_brightness = preferences.getUChar("led_brightness", 128);
    led_brightness = max(0, min(255, led_brightness));
    Serial.printf("[SYSTEM] Platform: %s (Keypad HID - layout indépendant)\n", platformDetected.c_str());
    
    // Keymap par défaut au démarrage
    apply_keymap_defaults();
    
    // Initialiser UART ATmega
    SerialAtmega.begin(ATMEGA_UART_BAUD, SERIAL_8N1, ATMEGA_UART_RX, ATMEGA_UART_TX);
    delay(100);
    Serial.printf("[UART] ATmega UART initialized TX=%d, RX=%d, %d baud\n",
                  ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD);
    
#if LED_PWM_PIN >= 0
    // PWM LED externe (si pin différent de la built-in)
    ledcSetup(led_pwm_channel, 1000, 10);
    ledcAttachPin(LED_PWM_PIN, led_pwm_channel);
    ledcWrite(led_pwm_channel, backlight_enabled ? (led_brightness * 1023 / 255) : 0);
    Serial.printf("[LED] LED PWM initialized on GPIO %d\n", LED_PWM_PIN);
#else
    Serial.println("[LED] Built-in LED only (NeoPixel), no PWM");
#endif
    
#if ENABLE_LED_STRIP
    ledStrip.begin();
    ledStrip.setBrightness(255);
    delay(10);
    update_builtin_led_from_light();
    Serial.printf("[LED] RGB initialized on GPIO %d (%d LED%s)\n", LED_STRIP_PIN, LED_STRIP_COUNT, LED_STRIP_COUNT > 1 ? "s" : "");
#else
    pinMode(LED_STRIP_PIN, OUTPUT);
    digitalWrite(LED_STRIP_PIN, LOW);
    Serial.println("[LED] RGB disabled");
#endif
    
    // Modules (logique événementielle)
    keyMatrix.begin();
    keyMatrix.setCallback(onKeyPress);
    Serial.println("[MATRIX] Key matrix initialized");

    encoder.begin();
    encoder.setRotateCallback(onEncoderRotate);
    encoder.setButtonCallback(onEncoderButton);
    Serial.println("[ENCODER] Rotary encoder initialized");

    hidOutput.begin(&Keyboard, &ConsumerControl);
    
    send_display_data_to_atmega();
    Serial.println("[MAIN] Initialization complete");
    Serial.println("Ready!");
}

// ==================== LOOP PRINCIPAL ====================

void loop() {
    unsigned long now = millis();
    
    // Lire l'encodeur AVANT le scan matrice (évite interférences GPIO sur CLK/DT)
    delay(1);
    encoder.update();
    keyMatrix.scan();

#if ENABLE_BLE_DEVICE_SWITCH
    // PROFILE(0,0) + 1(3,0) maintenu 2s → déconnecte BLE pour connecter un autre appareil
    bool profileHeld = keyMatrix.isKeyPressed(0, 0);
    bool oneHeld = keyMatrix.isKeyPressed(3, 0);
    if (profileHeld && oneHeld && (now - bleSwitchLastTrigger) > 2000) {
        if (bleSwitchComboStart == 0) bleSwitchComboStart = now;
        else if ((now - bleSwitchComboStart) >= BLE_SWITCH_COMBO_MS) {
            bleSwitchComboStart = 0;
            bleSwitchLastTrigger = now;
            if (BLE_AVAILABLE && pServer && pServer->getConnectedCount() > 0) {
                uint16_t connId = pServer->getConnId();
                pServer->disconnect(connId);
                Serial.println("[BLE] Switch appareil — déconnecté, prêt pour un autre appareil");
                send_last_key_to_atmega();
            }
        }
    } else {
        bleSwitchComboStart = 0;
    }
#endif

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
    
    // Luminosité ambiante: USB 30s, BLE 60s (pour LED + écran)
    unsigned long light_interval = deviceConnected ? LIGHT_POLL_INTERVAL_BLE_MS : LIGHT_POLL_INTERVAL_MS;
    if (now - last_light_poll >= light_interval) {
        send_light_level();
    }
    
    // Transition progressive de la LED
    update_builtin_led_from_light();
    
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
        if (!deviceConnected) send_light_level();  // BLE: pas de poll light (déconnexions)
    } else if (msg_type == "status") {
        send_status_message("Macropad ready");
    } else if (msg_type == "settings") {
        JsonObject settingsObj = doc.as<JsonObject>();
        if (settingsObj.containsKey("platform")) {
            platformDetected = settingsObj["platform"].as<String>();
            preferences.putString("platform", platformDetected);
        }
        if (settingsObj.containsKey("bleDeviceName")) {
            String name = settingsObj["bleDeviceName"].as<String>();
            preferences.putString("ble_device_name", name);
            Serial.printf("[CONFIG] BLE device name set: %s\n", name.c_str());
        }
    } else if (msg_type == "set_device_name") {
        if (doc.containsKey("name")) {
            String name = doc["name"].as<String>();
            preferences.putString("ble_device_name", name);
            Serial.printf("[CONFIG] BLE device name set: %s\n", name.c_str());
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
    send_display_data_to_atmega();
    send_status_message("Configuration updated");
}

void handle_backlight_message(JsonObject& data) {
    Serial.println("[WEB] Processing backlight message");
    
    if (data.containsKey("enabled")) {
        backlight_enabled = data["enabled"].as<bool>();
        if (!backlight_enabled) {
#if LED_PWM_PIN >= 0
            ledcWrite(led_pwm_channel, 0);
#endif
#if ENABLE_LED_STRIP
            ledStrip.clear();
            ledStrip.show();
#endif
        } else {
#if LED_PWM_PIN >= 0
            uint8_t pwm_val = (env_brightness_enabled && last_light_level >= LIGHT_THRESHOLD) ? 0 : led_brightness;
            ledcWrite(led_pwm_channel, pwm_val * 1023 / 255);
#endif
#if ENABLE_LED_STRIP
            ledStrip.setBrightness(led_brightness);
            update_per_key_leds();
#endif
        }
    }
    
    if (data.containsKey("brightness")) {
        led_brightness = data["brightness"].as<uint8_t>();
        led_brightness = max(0, min(255, led_brightness));
        if (backlight_enabled) {
            uint8_t pwm_val = (env_brightness_enabled && last_light_level >= LIGHT_THRESHOLD) ? 0 : led_brightness;
            ledcWrite(led_pwm_channel, pwm_val * 1023 / 255);
#if ENABLE_LED_STRIP
            ledStrip.setBrightness(led_brightness);
            update_per_key_leds();
#endif
        }
        Serial.printf("[LED] Brightness set to %d\n", led_brightness);
    }
    
    if (data.containsKey("envBrightness") || data.containsKey("env-brightness")) {
        env_brightness_enabled = data["envBrightness"].as<bool>() || data["env-brightness"].as<bool>();
        preferences.putBool("env_brightness", env_brightness_enabled);
        send_light_level();  // Mise à jour immédiate de la luminosité
    }
    
    // Persister backlight pour survie au reboot
    preferences.putBool("backlight_en", backlight_enabled);
    preferences.putUChar("led_brightness", (uint8_t)led_brightness);
    
#if ENABLE_LED_STRIP
    update_builtin_led_from_light();
#endif
    send_last_key_to_atmega();
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
    doc["bleDeviceName"] = preferences.getString("ble_device_name", "");
    
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

// Transition progressive: step plus grand = extinction plus rapide
#define LED_FADE_STEP 24
#define LED_FADE_INTERVAL_MS 15
static uint8_t led_current_r = 0, led_current_g = 0, led_current_b = 0;
static uint8_t led_target_r = 0, led_target_g = 0, led_target_b = 0;
static unsigned long last_led_fade = 0;

void update_builtin_led_from_light() {
#if ENABLE_LED_STRIP
    uint8_t tr, tg, tb;
    if (env_brightness_enabled) {
        // Toggle "Selon l'environnement" actif: light < 500 = ON (graduel), light >= 500 = OFF (graduel)
        bool is_dark;
#if LIGHT_SENSOR_INVERTED
        is_dark = (last_light_level >= LIGHT_THRESHOLD);
#else
        is_dark = (last_light_level < LIGHT_THRESHOLD);
#endif
        if (!is_dark) {
            tr = tg = tb = 0;
        } else {
            uint8_t v = backlight_enabled ? led_brightness : 80;
            if (v < 20) v = 20;
            tr = v;
            tg = (v * 180) / 255;
            tb = (v * 50) / 255;
        }
    } else {
        // Toggle désactivé: luminosité manuelle (backlight_enabled + led_brightness)
        if (backlight_enabled) {
            uint8_t v = led_brightness;
            if (v < 20) v = 20;
            tr = v;
            tg = (v * 180) / 255;
            tb = (v * 50) / 255;
        } else {
            tr = tg = tb = 0;
        }
    }
    led_target_r = tr;
    led_target_g = tg;
    led_target_b = tb;
    
    // Transition progressive (toutes les ~20ms)
    unsigned long now = millis();
    if (now - last_led_fade >= LED_FADE_INTERVAL_MS) {
        last_led_fade = now;
        uint8_t step = LED_FADE_STEP;
        if (led_current_r < led_target_r) {
            led_current_r = (led_target_r - led_current_r <= step) ? led_target_r : led_current_r + step;
        } else if (led_current_r > led_target_r) {
            led_current_r = (led_current_r - led_target_r <= step) ? led_target_r : led_current_r - step;
        }
        if (led_current_g < led_target_g) {
            led_current_g = (led_target_g - led_current_g <= step) ? led_target_g : led_current_g + step;
        } else if (led_current_g > led_target_g) {
            led_current_g = (led_current_g - led_target_g <= step) ? led_target_g : led_current_g - step;
        }
        if (led_current_b < led_target_b) {
            led_current_b = (led_target_b - led_current_b <= step) ? led_target_b : led_current_b + step;
        } else if (led_current_b > led_target_b) {
            led_current_b = (led_current_b - led_target_b <= step) ? led_target_b : led_current_b - step;
        }
    }
    
    ledStrip.setPixelColor(0, ledStrip.Color(led_current_r, led_current_g, led_current_b));
    ledStrip.show();
#endif

#if LED_PWM_PIN >= 0
    // PWM LED externe (si présent)
    if (env_brightness_enabled && last_light_level >= LIGHT_THRESHOLD) {
        ledcWrite(led_pwm_channel, 0);
    } else if (env_brightness_enabled && last_light_level < LIGHT_THRESHOLD) {
        uint8_t v = backlight_enabled ? led_brightness : 80;
        ledcWrite(led_pwm_channel, v * 1023 / 255);
    }
#endif
}

void update_per_key_leds() {
#if ENABLE_LED_STRIP
    update_builtin_led_from_light();
#endif
}

void set_key_led_pressed(int row, int col, bool pressed) {
#if ENABLE_LED_STRIP
    // Pas de flash blanc à l'appui — la LED reste sur la luminosité ambiante
    if (!pressed) {
        update_builtin_led_from_light();
    }
#endif
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

void send_uart_log_to_web(const char* dir, const char* msg) {
    unsigned long now = millis();
    if (deviceConnected && (now - last_uart_log_to_web) < UART_LOG_TO_WEB_INTERVAL_MS) {
        return;  // Throttle: éviter flood BLE
    }
    last_uart_log_to_web = now;
    String json = "{\"type\":\"uart_log\",\"dir\":\"";
    json += dir;
    json += "\",\"msg\":\"";
    for (const char* p = msg; *p; p++) {
        if (*p == '"' || *p == '\\') json += '\\';
        json += *p;
    }
    json += "\"}";
    send_to_web(json);
}

void send_to_web(String data) {
    Serial.println(data);
    if (deviceConnected && BLE_AVAILABLE && pSerialCharacteristic != nullptr) {
        String message = data + "\n";
        pSerialCharacteristic->setValue(message.c_str());
        pSerialCharacteristic->notify();
    }
}

// Envoyer la luminosité au web (USB et BLE). Throttle 2s pour éviter flood BLE.
void send_light_to_web_if_needed(uint16_t light_value) {
    unsigned long now = millis();
    bool value_changed = (light_value != last_light_sent_to_web);
    bool interval_elapsed = (now - last_light_send_time >= LIGHT_SEND_MIN_INTERVAL_MS);
    if (value_changed || interval_elapsed) {
        last_light_sent_to_web = light_value;
        last_light_send_time = now;
        String msg = "{\"type\":\"light\",\"level\":" + String(light_value) + "}";
        send_to_web(msg);  // USB: Serial | BLE: notify
        send_last_key_to_atmega();  // Mettre à jour le statut rétro-éclairage sur l'écran
    }
    update_builtin_led_from_light();
}

void send_atmega_command(uint8_t cmd, uint8_t* payload, int payload_len) {
    SerialAtmega.write(cmd);
    if (payload != nullptr && payload_len > 0) {
        SerialAtmega.write(payload, payload_len);
    }
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    Serial.printf("[UART] Sent command 0x%02X (%d bytes payload)\n", cmd, payload_len);
    
    // Log vers la console web (sauf CMD_READ_LIGHT et CMD_SET_LAST_KEY pour éviter flood BLE)
    if (cmd != CMD_READ_LIGHT && cmd != CMD_SET_LAST_KEY) {
        char buf[128];
        int n = 0;
        if (payload_len > 0 && payload != nullptr) {
            n = snprintf(buf, sizeof(buf), "CMD 0x%02X + %d bytes", cmd, payload_len);
            if (payload_len <= 8 && n < (int)sizeof(buf) - 4) {
                n += snprintf(buf + n, sizeof(buf) - n, " [");
                for (int i = 0; i < payload_len && n < (int)sizeof(buf) - 4; i++) {
                    n += snprintf(buf + n, sizeof(buf) - n, "%02X ", payload[i]);
                }
                if (n < (int)sizeof(buf) - 2) snprintf(buf + n, sizeof(buf) - n, "]");
            }
        } else {
            const char* names[] = {"", "READ_LIGHT", "SET_LED", "GET_LED", "UPDATE_DISPLAY", "SET_DISPLAY_DATA", "", "", "SET_IMAGE", "IMAGE_CHUNK", "ATMEGA_DEBUG", "ATMEGA_LOG"};
            const char* name = (cmd < 12) ? names[cmd] : "?";
            snprintf(buf, sizeof(buf), "CMD 0x%02X %s", cmd, name);
        }
        send_uart_log_to_web("tx", buf);
    }
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
    
    // Vérifier si c'est une réponse binaire CMD_READ_LIGHT [0x01][low][high][\n]
    if (data.length() >= 3 && (uint8_t)data.charAt(0) == CMD_READ_LIGHT) {
        uint16_t light_value = (uint8_t)data.charAt(1) | ((uint8_t)data.charAt(2) << 8);
        last_light_level = light_value;
        Serial.printf("[ATMEGA LIGHT] Level (binary): %d\n", light_value);
        send_light_to_web_if_needed(light_value);
        return;
    }
    
    // Traiter les messages texte
    atmega_rx_buffer += data;
    
    int newlinePos;
    while ((newlinePos = atmega_rx_buffer.indexOf('\n')) >= 0) {
        String line = atmega_rx_buffer.substring(0, newlinePos);
        line.trim();  // Enlever \r si présent
        atmega_rx_buffer = atmega_rx_buffer.substring(newlinePos + 1);
        
        if (line.length() == 0) continue;
        
        // Réponse binaire CMD_READ_LIGHT (reçu par morceaux)
        if (line.length() >= 3 && (uint8_t)line.charAt(0) == CMD_READ_LIGHT) {
            uint16_t light_value = (uint8_t)line.charAt(1) | ((uint8_t)line.charAt(2) << 8);
            last_light_level = light_value;
            Serial.printf("[ATMEGA LIGHT] Level (binary): %d\n", light_value);
            send_light_to_web_if_needed(light_value);
            continue;
        }
        // Format ASCII "LIGHT=XXX"
        if (line.startsWith("LIGHT=")) {
            uint16_t light_value = line.substring(6).toInt();
            last_light_level = light_value;
            Serial.printf("[ATMEGA LIGHT] Level (ASCII): %d\n", light_value);
            send_light_to_web_if_needed(light_value);
            continue;
        }
        // Format debug ATmega "[LIGHT] Level: NNN (0x...)"
        if (line.startsWith("[LIGHT] Level: ")) {
            int spacePos = line.indexOf(' ', 15);
            if (spacePos > 15) {
                uint16_t light_value = (uint16_t)line.substring(15, spacePos).toInt();
                if (light_value <= 1023) {
                    last_light_level = light_value;
                    send_light_to_web_if_needed(light_value);
                }
            }
            continue;
        }
        Serial.printf("[ATMEGA] %s\n", line.c_str());
        send_uart_log_to_web("rx", line.c_str());
    }
}

void send_light_level() {
    unsigned long now = millis();
    if (last_light_poll != 0 && (now - last_light_poll) < LIGHT_POLL_MIN_INTERVAL_MS) return;
    last_light_poll = now;
    send_atmega_command(CMD_READ_LIGHT, nullptr, 0);
    send_light_to_web_if_needed(last_light_level);
}

void send_last_key_to_atmega() {
    String last_key = last_key_pressed.length() > 0 ? last_key_pressed : "";
    int len = last_key.length();
    if (len > 15) len = 15;
    uint8_t payload[20];
    int pos = 0;
    payload[pos++] = len & 0xFF;
    if (len > 0) {
        memcpy(&payload[pos], last_key.c_str(), len);
        pos += len;
    }
    // Rétro-éclairage pour l'écran: selon env_brightness_enabled ou manuel
    int back_en;
    uint8_t back_val;
    if (env_brightness_enabled) {
#if LIGHT_SENSOR_INVERTED
        back_en = (last_light_level >= LIGHT_THRESHOLD) ? 1 : 0;
#else
        back_en = (last_light_level < LIGHT_THRESHOLD) ? 1 : 0;
#endif
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    } else {
        back_en = backlight_enabled ? 1 : 0;
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    }
    payload[pos++] = back_en & 0xFF;
    payload[pos++] = back_val;
    send_atmega_command(CMD_SET_LAST_KEY, payload, pos);
}

uint8_t count_configured_keys() {
    uint8_t count = 0;
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            if (KEYMAP[r][c].length() > 0) count++;
        }
    }
    return count;
}

void send_display_data_to_atmega() {
    uint8_t payload[80];
    int pos = 0;
    payload[pos++] = led_brightness;
    const char* mode = "data";
    uint8_t mode_len = strlen(mode);
    payload[pos++] = mode_len;
    memcpy(&payload[pos], mode, mode_len);
    pos += mode_len;
    const char* profile = "Profil 1";
    uint8_t profile_len = strlen(profile);
    payload[pos++] = profile_len;
    memcpy(&payload[pos], profile, profile_len);
    pos += profile_len;
    const char* output = deviceConnected ? "bluetooth" : "usb";
    uint8_t output_len = strlen(output);
    payload[pos++] = output_len;
    memcpy(&payload[pos], output, output_len);
    pos += output_len;
    payload[pos++] = count_configured_keys();
    uint8_t last_key_len = min((int)last_key_pressed.length(), 15);
    payload[pos++] = last_key_len;
    if (last_key_len > 0) {
        memcpy(&payload[pos], last_key_pressed.c_str(), last_key_len);
        pos += last_key_len;
    }
    int back_en;
    uint8_t back_val;
    if (env_brightness_enabled) {
#if LIGHT_SENSOR_INVERTED
        back_en = (last_light_level >= LIGHT_THRESHOLD) ? 1 : 0;
#else
        back_en = (last_light_level < LIGHT_THRESHOLD) ? 1 : 0;
#endif
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    } else {
        back_en = backlight_enabled ? 1 : 0;
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    }
    payload[pos++] = back_en & 0xFF;
    payload[pos++] = back_val;
    send_atmega_command(CMD_SET_DISPLAY_DATA, payload, pos);
}
