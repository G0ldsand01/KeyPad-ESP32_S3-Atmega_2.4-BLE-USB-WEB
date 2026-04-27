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
#include <Update.h>
#include <string.h>
#include <string>
#include <math.h>
#include <stdarg.h>

#if USE_ESP32_DISPLAY_ST7789
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#endif

extern Preferences preferences;

// ==================== LOGGING HELPERS ====================
static void log_line(const char* tag, const char* fmt, ...) {
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Serial.printf("[%-5s] %s\n", tag, msg);
}

static bool debug_web_enabled() {
    return preferences.getBool("dbg_web", false);
}

// Boot box (encadré) — pour un log "propre" et lisible.
static const int BOOT_BOX_W = 66; // largeur totale (incluant bordures)
static void boot_box_hr() {
    Serial.print("+");
    for (int i = 0; i < BOOT_BOX_W - 2; i++) Serial.print("-");
    Serial.println("+");
}
static void boot_box_line(const char* s) {
    // Contenu max = W - 4 (bordures + espaces)
    const int inner = BOOT_BOX_W - 4;
    char buf[256];
    snprintf(buf, sizeof(buf), "%.*s", inner, s ? s : "");
    int len = (int)strlen(buf);
    Serial.print("| ");
    Serial.print(buf);
    for (int i = len; i < inner; i++) Serial.print(" ");
    Serial.println(" |");
}
static void boot_box_kv(const char* k, const char* v) {
    char line[256];
    snprintf(line, sizeof(line), "%s: %s", k ? k : "", v ? v : "");
    boot_box_line(line);
}

// Base64 decode minimal (évite dépendance mbedtls)
static int base64_decode(const char* in, size_t inlen, uint8_t* out, size_t outmax, size_t* outlen) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    size_t o = 0;
    uint32_t v = 0;
    int n = 0;
    for (size_t i = 0; i < inlen; i++) {
        int c = T[(unsigned char)in[i]];
        if (c == -1) continue;
        if (c == -2) break;  // padding
        v = (v << 6) | (c & 0x3F);
        n++;
        if (n == 4) {
            if (o + 3 > outmax) return -1;
            out[o++] = (v >> 16) & 0xFF;
            out[o++] = (v >> 8) & 0xFF;
            out[o++] = v & 0xFF;
            n = 0;
        }
    }
    if (n == 2) { if (o + 1 > outmax) return -1; out[o++] = (v >> 4) & 0xFF; }
    else if (n == 3) { if (o + 2 > outmax) return -1; out[o++] = (v >> 10) & 0xFF; out[o++] = (v >> 2) & 0xFF; }
    *outlen = o;
    return 0;
}

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

// Profils (côté firmware) — 3 profils cyclables via touche PROFILE (0,0)
static const uint8_t PROFILE_COUNT_DEFAULT = 3;
uint8_t profileCount = PROFILE_COUNT_DEFAULT;
uint8_t activeProfileIndex = 0;  // 0 = Profil 1

// Forward declarations nécessaires pour les helpers de profils
void apply_keymap_defaults();
void send_display_data_to_atmega();
void send_config_to_web();
void send_status_message(String message);

static String profileName(uint8_t idx) {
    return "Profil " + String((int)idx + 1);
}

static String getStoredProfileName(uint8_t idx) {
    String k = "profile_name_" + String((int)idx);
    String name = preferences.getString(k.c_str(), "");
    if (name.length() == 0) return profileName(idx);
    return name;
}

static void storeProfileName(uint8_t idx, const String& name) {
    String k = "profile_name_" + String((int)idx);
    preferences.putString(k.c_str(), name);
}

static void loadProfileKeymap(uint8_t idx) {
    // Charger la keymap du profil depuis NVS (Preferences). Si une clé manque: laisser la valeur courante.
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            // 0-0 est réservé à PROFILE (switch profil) — ne jamais le surcharger depuis NVS
            if (r == 0 && c == 0) continue;
            String keyName = "p" + String((int)idx) + "_k_" + String(r) + "_" + String(c);
            if (preferences.isKey(keyName.c_str())) {
                // Important: sur certains profils "vides", des clés existent mais avec valeur "".
                // Si on charge "", ça efface les defaults et aucune touche n'envoie rien.
                // On ignore donc les valeurs vides et on conserve le default courant.
                String v = preferences.getString(keyName.c_str(), "");
                if (v.length() > 0) KEYMAP[r][c] = v;
            }
        }
    }
}

static void saveProfileKeymap(uint8_t idx) {
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            // 0-0 est réservé à PROFILE (switch profil) — ne pas persister
            if (r == 0 && c == 0) continue;
            String keyName = "p" + String((int)idx) + "_k_" + String(r) + "_" + String(c);
            preferences.putString(keyName.c_str(), KEYMAP[r][c]);
        }
    }
}

static void setActiveProfile(uint8_t idx) {
    if (profileCount == 0) profileCount = PROFILE_COUNT_DEFAULT;
    activeProfileIndex = idx % profileCount;
    preferences.putUChar("profile_count", profileCount);
    preferences.putUChar("active_profile", activeProfileIndex);
}

static void switchToNextProfile() {
    uint8_t next = (activeProfileIndex + 1) % (profileCount ? profileCount : PROFILE_COUNT_DEFAULT);
    setActiveProfile(next);
    apply_keymap_defaults();       // base stable (PROFILE reste en 0,0)
    loadProfileKeymap(activeProfileIndex);
}

// UART ATmega
    String atmega_rx_buffer = "";
unsigned long null_bytes_count = 0;
unsigned long last_null_warning = 0;
uint16_t last_light_level = 0;
// 0 = ATmega (UART CMD_READ_LIGHT), 1 = ESP32 (ADC analogRead)
uint8_t light_source = 0;
// --- Display (contrôlé par l'ATmega) ---
static uint8_t display_brightness = 128;
static char display_mode_str[8] = "data"; // "data" / "image" / "gif" (support local: data)

#if USE_ESP32_DISPLAY_ST7789
// Utiliser le bus SPI par défaut (plus fiable sur ESP32-S3 avec pins remappées).
static Adafruit_ST7789 esp32Tft(&SPI, ESP32_TFT_CS, ESP32_TFT_DC, ESP32_TFT_RST);
static bool esp32DisplayReady = false;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void init_esp32_display_if_needed() {
    if (esp32DisplayReady) return;

#if ESP32_TFT_BL >= 0
    // Stabiliser la backlight en PWM (évite conflits digitalWrite/ledc et clignotements).
    const int ch = 7;
    ledcSetup(ch, 5000, 8);
    ledcAttachPin(ESP32_TFT_BL, ch);
    ledcWrite(ch, ESP32_TFT_BL_INVERT ? 0 : 255);
#endif

    Serial.printf("[TFT] init pins SCK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d\n",
                  ESP32_TFT_SCK, ESP32_TFT_MOSI, ESP32_TFT_CS, ESP32_TFT_DC, ESP32_TFT_RST, ESP32_TFT_BL);
    {
        char msg[140];
        snprintf(msg, sizeof(msg), "TFT init pins SCK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d",
                 ESP32_TFT_SCK, ESP32_TFT_MOSI, ESP32_TFT_CS, ESP32_TFT_DC, ESP32_TFT_RST, ESP32_TFT_BL);
        send_status_message(String(msg));
    }

    // Reset hard (si RST est câblé)
    pinMode(ESP32_TFT_RST, OUTPUT);
    digitalWrite(ESP32_TFT_RST, HIGH);
    delay(10);
    digitalWrite(ESP32_TFT_RST, LOW);
    delay(40);
    digitalWrite(ESP32_TFT_RST, HIGH);
    delay(120);

    const int cs = (ESP32_TFT_CS >= 0) ? ESP32_TFT_CS : -1;
    SPI.begin(ESP32_TFT_SCK, -1, ESP32_TFT_MOSI, cs);
    // ST7789: selon module, 240x320 est un init safe pour beaucoup d'écrans.
    esp32Tft.init(240, 320);
    esp32Tft.setSPISpeed(8000000); // plus lent pour debug
    esp32Tft.setRotation(1);
    esp32Tft.fillScreen(ST77XX_BLACK);
    esp32Tft.setTextWrap(false);
    esp32DisplayReady = true;
    Serial.println("[TFT] init done");
    send_status_message("TFT init done");
}
#endif

// Filtrage + hystérésis pour éviter le "clignotement" autour du seuil
static float light_filtered = 0.0f;
static bool light_is_dark = false;
static uint16_t clamp_u16(int v, int lo, int hi) {
    if (v < lo) return (uint16_t)lo;
    if (v > hi) return (uint16_t)hi;
    return (uint16_t)v;
}

// Met à jour light_is_dark avec hystérésis.
// Calculée en continu (ne dépend pas de l'ordre de déclaration des toggles).
static void update_light_hysteresis_from_levels() {
#if LIGHT_SENSOR_INVERTED
    const uint16_t th_lo = clamp_u16((int)LIGHT_THRESHOLD - 30, 0, 1023);
    const uint16_t th_hi = clamp_u16((int)LIGHT_THRESHOLD + 30, 0, 1023);
    if (!light_is_dark && last_light_level >= th_hi) light_is_dark = true;
    else if (light_is_dark && last_light_level <= th_lo) light_is_dark = false;
#else
    const uint16_t th_lo = clamp_u16((int)LIGHT_THRESHOLD - 30, 0, 1023);
    const uint16_t th_hi = clamp_u16((int)LIGHT_THRESHOLD + 30, 0, 1023);
    if (!light_is_dark && last_light_level <= th_lo) light_is_dark = true;
    else if (light_is_dark && last_light_level >= th_hi) light_is_dark = false;
#endif
}

#if USE_ESP32_DISPLAY_ST7789
// Forward declarations: render_display_data_local() est défini avant certains globals.
extern bool deviceConnected;
extern bool env_brightness_enabled;
extern bool backlight_enabled;
extern String last_key_pressed;
uint8_t count_configured_keys();

static void tft_hw_pin_test() {
#if ESP32_TFT_HW_PIN_TEST
    send_status_message("TFT HW test: start");
    Serial.println("[TFT] HW test: start");

    // RST pulse
    pinMode(ESP32_TFT_RST, OUTPUT);
    digitalWrite(ESP32_TFT_RST, HIGH);
    delay(20);
    digitalWrite(ESP32_TFT_RST, LOW);
    delay(50);
    digitalWrite(ESP32_TFT_RST, HIGH);
    delay(120);
    Serial.println("[TFT] HW test: RST pulse done");

    // CS/DC toggle
    if (ESP32_TFT_CS >= 0) {
        pinMode(ESP32_TFT_CS, OUTPUT);
        digitalWrite(ESP32_TFT_CS, HIGH);
    }
    pinMode(ESP32_TFT_DC, OUTPUT);
    for (int i = 0; i < 6; i++) {
        if (ESP32_TFT_CS >= 0) digitalWrite(ESP32_TFT_CS, (i & 1) ? LOW : HIGH);
        digitalWrite(ESP32_TFT_DC, (i & 1) ? HIGH : LOW);
        delay(120);
    }
    if (ESP32_TFT_CS >= 0) digitalWrite(ESP32_TFT_CS, HIGH);
    Serial.println("[TFT] HW test: CS/DC toggle done");

    // Backlight PWM sweep (si BL pin câblée)
#if ESP32_TFT_BL >= 0
    const int ch = 7;
    ledcSetup(ch, 5000, 8);
    ledcAttachPin(ESP32_TFT_BL, ch);
    // Sweep up/down 2 fois
    for (int rep = 0; rep < 2; rep++) {
        for (int v = 0; v <= 255; v += 5) {
            uint8_t out = (uint8_t)v;
            if (ESP32_TFT_BL_INVERT) out = 255 - out;
            ledcWrite(ch, out);
            delay(10);
        }
        for (int v = 255; v >= 0; v -= 5) {
            uint8_t out = (uint8_t)v;
            if (ESP32_TFT_BL_INVERT) out = 255 - out;
            ledcWrite(ch, out);
            delay(10);
        }
    }
    // Laisser ON
    ledcWrite(ch, ESP32_TFT_BL_INVERT ? 0 : 255);
    Serial.println("[TFT] HW test: BL PWM sweep done");
#else
    Serial.println("[TFT] HW test: BL pin not set");
#endif

    // SPI burst (sans dépendre de l'init ST7789)
    const int cs = (ESP32_TFT_CS >= 0) ? ESP32_TFT_CS : -1;
    SPI.begin(ESP32_TFT_SCK, -1, ESP32_TFT_MOSI, cs);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, ESP32_TFT_SPI_MODE == 3 ? SPI_MODE3 : SPI_MODE0));
    if (ESP32_TFT_CS >= 0 && !ESP32_TFT_CS_GND) digitalWrite(ESP32_TFT_CS, LOW);
    digitalWrite(ESP32_TFT_DC, LOW);
    SPI.transfer(0x00);
    digitalWrite(ESP32_TFT_DC, HIGH);
    for (int i = 0; i < 512; i++) SPI.transfer((uint8_t)i);
    if (ESP32_TFT_CS >= 0 && !ESP32_TFT_CS_GND) digitalWrite(ESP32_TFT_CS, HIGH);
    SPI.endTransaction();
    Serial.println("[TFT] HW test: SPI burst sent");

#if ESP32_TFT_MANUAL_ST7789_TEST
    // --- Test manuel ST7789: init + remplissage RAM ---
    auto cs_select = []() {
        if (ESP32_TFT_CS >= 0 && !ESP32_TFT_CS_GND) digitalWrite(ESP32_TFT_CS, LOW);
    };
    auto cs_deselect = []() {
        if (ESP32_TFT_CS >= 0 && !ESP32_TFT_CS_GND) digitalWrite(ESP32_TFT_CS, HIGH);
    };
    auto wr_cmd = [&](uint8_t cmd) {
        cs_select();
        digitalWrite(ESP32_TFT_DC, LOW);
        SPI.transfer(cmd);
        cs_deselect();
    };
    auto wr_data = [&](const uint8_t* d, int n) {
        cs_select();
        digitalWrite(ESP32_TFT_DC, HIGH);
        for (int i = 0; i < n; i++) SPI.transfer(d[i]);
        cs_deselect();
    };
    auto wr_data1 = [&](uint8_t b) {
        wr_data(&b, 1);
    };

    send_status_message("TFT manual ST7789: init");
    Serial.println("[TFT] manual ST7789: init");
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, ESP32_TFT_SPI_MODE == 3 ? SPI_MODE3 : SPI_MODE0));

    wr_cmd(0x01); // SWRESET
    delay(150);
    wr_cmd(0x11); // SLPOUT
    delay(150);
    wr_cmd(0x3A); // COLMOD
    wr_data1(0x55); // 16-bit (RGB565)
    delay(10);
    wr_cmd(0x36); // MADCTL
    wr_data1(0x00);
    wr_cmd(0x29); // DISPON
    delay(120);

    // CASET / RASET (240x320)
    wr_cmd(0x2A);
    { uint8_t d[4] = {0x00,0x00,0x00,0xEF}; wr_data(d, 4); } // 0..239
    wr_cmd(0x2B);
    { uint8_t d[4] = {0x00,0x00,0x01,0x3F}; wr_data(d, 4); } // 0..319
    wr_cmd(0x2C); // RAMWR

    // Remplir quelques lignes en magenta/vert alterné (visible si le contrôleur reçoit).
    cs_select();
    digitalWrite(ESP32_TFT_DC, HIGH);
    for (int y = 0; y < 40; y++) {
        uint16_t c = (y & 1) ? 0xF81F : 0x07E0; // magenta / vert
        uint8_t hi = (uint8_t)(c >> 8), lo = (uint8_t)(c & 0xFF);
        for (int x = 0; x < 240; x++) { SPI.transfer(hi); SPI.transfer(lo); }
    }
    cs_deselect();
    SPI.endTransaction();
    send_status_message("TFT manual ST7789: wrote 40 lines");
    Serial.println("[TFT] manual ST7789: wrote 40 lines");
#endif

    send_status_message("TFT HW test: done (check BL fade)");
#endif
}

static void render_display_data_local() {
    init_esp32_display_if_needed();
    if (!esp32DisplayReady) return;

    static bool first = true;
    if (!first) return; // dessiner une seule fois pour debug (évite "blink" visuel)
    first = false;

    // Test visuel simple (pas d'alternance): fond blanc + texte noir.
    const uint16_t bg = ST77XX_WHITE;
    const uint16_t fg = ST77XX_BLACK;
    esp32Tft.fillScreen(bg);
    esp32Tft.setTextWrap(false);
    esp32Tft.setTextSize(4);
    esp32Tft.setTextColor(fg, bg);
    esp32Tft.setCursor(8, 8);
    esp32Tft.print("HI");
    Serial.println("[TFT] draw HI");

    static unsigned long last_tft_status = 0;
    unsigned long now = millis();
    if (now - last_tft_status > 2500UL) {
        last_tft_status = now;
        send_status_message("TFT draw HI");
    }
}
#endif
static uint16_t read_esp32_light_level_0_1023() {
#if USE_ESP32_LIGHT_SENSOR
    int raw = analogRead(ESP32_LIGHT_ADC_PIN);
    if (raw < 0) raw = 0;
    if (raw > ESP32_LIGHT_ADC_MAX) raw = ESP32_LIGHT_ADC_MAX;
    // Normaliser 0..ESP32_LIGHT_ADC_MAX vers 0..1023
    uint32_t v = (uint32_t)raw * 1023u;
    v /= (uint32_t)max(1, (int)ESP32_LIGHT_ADC_MAX);
    if (v > 1023u) v = 1023u;
    return (uint16_t)v;
#else
    return last_light_level;
#endif
}

// LED
int led_pwm_channel = 0;
int led_brightness = 128;
bool backlight_enabled = true;
bool env_brightness_enabled = false;  // Toggle "Selon l'environnement" du web
// Couleur de base du rétroéclairage RVB (0–255), combinée à led_brightness dans update_builtin_led_from_light
uint8_t led_color_r = 255;
uint8_t led_color_g = 180;
uint8_t led_color_b = 50;
uint8_t encoderStep = 1; // Multiplicateur de pas pour l'encodeur (1..10)

// BLE (Bluedroid — Android, Windows, compatible)
BLEServer* pServer = nullptr;
BLECharacteristic* pInputCharacteristic = nullptr;
BLECharacteristic* pSerialCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String bleSerialBuffer = "";
bool BLE_AVAILABLE = false;

String platformDetected = "unknown";
Adafruit_NeoPixel ledStrip(LED_STRIP_MAX, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// Longueur active de la chaîne (≤ LED_STRIP_MAX) + animations rétroéclairage
uint16_t led_strip_active_count = LED_STRIP_DEFAULT_ACTIVE;
#define BL_ANIM_SOLID    0
#define BL_ANIM_BREATHE  1
#define BL_ANIM_RAINBOW  2
#define BL_ANIM_CHASE    3
#define BL_ANIM_SCANNER  4
#define BL_ANIM_SPARKLE  5
uint8_t bl_anim_mode = BL_ANIM_SOLID;
uint16_t bl_anim_speed_ms = 2500;  // période (respiration / arc-en-ciel / rebond scanner)
uint8_t bl_anim_length = 3;         // largeur traînée (poursuite / scanner), 1–16

// LEDs: selon le câblage, la LED intégrée peut être:
// - en série avant les touches (pixel 0 réservé)
// - ou en parallèle (elle "copie" le pixel 0 des touches, impossible à séparer en software)
static constexpr uint16_t BUILTIN_PIXEL_INDEX = 0;
static constexpr uint16_t KEY_PIXELS_OFFSET =
#if LED_STRIP_FIRST_PIXEL_RESERVED
  1
#else
  0
#endif
  ;

// OTA
bool ota_in_progress = false;
int ota_chunk_count = 0;
int ota_total_chunks = 0;
size_t ota_file_size = 0;
#define OTA_DECODE_BUF_SIZE 384  // Base64 decode buffer (256 bytes raw -> 344 chars base64)

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

    // PROFILE: changer de profil côté firmware (ne pas envoyer au host)
    if (symbol == "PROFILE") {
        Serial.println("[PROFILE] Switch profile");
        switchToNextProfile();
        send_display_data_to_atmega();
        send_config_to_web();
    send_status_message("Profil actif: " + getStoredProfileName(activeProfileIndex));
        return;
    }

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
    uint16_t total = (uint16_t)steps * (uint16_t)max((uint8_t)1, min((uint8_t)10, encoderStep));
    if (total == 0) total = 1;
    for (uint16_t i = 0; i < total; i++) {
        if (dir > 0) hidOutput.sendVolumeUp();
        else hidOutput.sendVolumeDown();
        // Android BLE: espacement requis entre rapports Consumer (sinon "volume max ou rien")
        if (deviceConnected && i < total - 1) delay(BLE_VOLUME_STEP_DELAY_MS);
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
void apply_led_strip_runtime_config();
static const char* anim_mode_to_string(uint8_t m);

// ==================== CALLBACKS BLE ====================

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pSrv) override {
        deviceConnected = true;
        hidOutput.setBleState(true, pInputCharacteristic);
        Serial.println("[BLE] Client connected");
    }
    void onDisconnect(BLEServer* pSrv) override {
        deviceConnected = false;
        hidOutput.setBleState(false, nullptr);
        Serial.println("[BLE] Client disconnected");
    }
};

class SerialCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (!value.empty()) {
            bleSerialBuffer += String(value.c_str());
        }
    }
};

// ==================== SETUP ====================

void setup() {
    // IMPORTANT: Tools > USB CDC On Boot: Enabled = Serial sur port USB natif.
    //            Disabled = HID seul sur port USB natif; utiliser port UART pour Serial/flash.
    // Délai pour laisser le port USB s'initialiser après le boot
    delay(2000);
    
    Serial.begin(115200);
    delay(500);
    randomSeed((unsigned long)(micros() ^ millis()));
    // Boot: on affiche un encadré récapitulatif plus bas (après init prefs/UART).
    
    // Initialiser USB HID (clavier + Consumer Control pour volume/média)
    USB.begin();
    delay(1000);
    Keyboard.begin();
    ConsumerControl.begin();
    delay(1000);
    // (logs détaillés USB/BLE dans l'encadré de boot)
    
    // Initialiser BLE avec un nom qui indique clairement que c'est un clavier
    // iPhone/iOS reconnaît mieux les appareils avec "Keyboard" dans le nom
    try {
        BLEDevice::init("Macropad Keyboard");
        
        // Configurer la sécurité BLE — évite échecs d’appairage iOS sur HID personnalisés
        BLESecurity* pSecurity = new BLESecurity();
        pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
        pSecurity->setCapability(ESP_IO_CAP_NONE);
        pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());
        BLEService* pService = pServer->createService(BLEUUID((uint16_t)0x1812));
        uint8_t info[] = {0x01, 0x01, 0x00, 0x03};
        BLECharacteristic* pInfo = pService->createCharacteristic(BLEUUID((uint16_t)0x2A4A), BLECharacteristic::PROPERTY_READ);
        pInfo->setValue(info, 4);
        uint8_t reportMap[] = {
            0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
            0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
            0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x81,
            0x05, 0x07, 0x19, 0x00, 0x29, 0x81, 0x81, 0x00, 0xC0,
            0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
            0x15, 0x00, 0x26, 0x9C, 0x02, 0x75, 0x10, 0x95, 0x01, 0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xCD, 0x81, 0x00, 0xC0
        };
        BLECharacteristic* pMap = pService->createCharacteristic(BLEUUID((uint16_t)0x2A4B), BLECharacteristic::PROPERTY_READ);
        pMap->setValue(reportMap, sizeof(reportMap));
        uint8_t proto = 0x01;
        BLECharacteristic* pProto = pService->createCharacteristic(BLEUUID((uint16_t)0x2A4E), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR);
        pProto->setValue(&proto, 1);
        uint8_t ctrl = 0x00;
        BLECharacteristic* pCtrl = pService->createCharacteristic(BLEUUID((uint16_t)0x2A4C), BLECharacteristic::PROPERTY_WRITE_NR);
        pCtrl->setValue(&ctrl, 1);
        pInputCharacteristic = pService->createCharacteristic(
            BLEUUID((uint16_t)0x2A4D),
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE_NR
        );
        pInputCharacteristic->addDescriptor(new BLE2902());
        uint8_t empty_report[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        pInputCharacteristic->setValue(empty_report, 9);
        pService->start();
        BLEService* pDevInfo = pServer->createService(BLEUUID((uint16_t)0x180A));
        BLECharacteristic* pMfr = pDevInfo->createCharacteristic(BLEUUID((uint16_t)0x2A29), BLECharacteristic::PROPERTY_READ);
        pMfr->setValue("Macropad");
        BLECharacteristic* pModel = pDevInfo->createCharacteristic(BLEUUID((uint16_t)0x2A24), BLECharacteristic::PROPERTY_READ);
        pModel->setValue("Keyboard");
        pDevInfo->start();
        BLEService* pBat = pServer->createService(BLEUUID((uint16_t)0x180F));
        BLECharacteristic* pBatLev = pBat->createCharacteristic(BLEUUID((uint16_t)0x2A19), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
        pBatLev->addDescriptor(new BLE2902());
        uint8_t bat = 100;
        pBatLev->setValue(&bat, 1);
        pBat->start();
        BLEService* pSerialSvc = pServer->createService(BLEUUID(SERVICE_UUID_SERIAL));
        pSerialCharacteristic = pSerialSvc->createCharacteristic(BLEUUID(CHAR_UUID_SERIAL), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE_NR);
        pSerialCharacteristic->addDescriptor(new BLE2902());
        pSerialCharacteristic->setCallbacks(new SerialCharacteristicCallbacks());
        pSerialSvc->start();
        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x1812));
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180A));
        pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180F));
        pAdvertising->addServiceUUID(BLEUUID(SERVICE_UUID_SERIAL));
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMaxPreferred(0x12);
        BLEDevice::startAdvertising();
        BLE_AVAILABLE = true;
        // (logs détaillés USB/BLE dans l'encadré de boot)
    } catch (...) {
        BLE_AVAILABLE = false;
        Serial.println("[BLE] Error initializing BLE");
    }
    
    preferences.begin("macropad", false);
    platformDetected = preferences.getString("platform", "unknown");
    profileCount = preferences.getUChar("profile_count", PROFILE_COUNT_DEFAULT);
    if (profileCount == 0 || profileCount > 10) profileCount = PROFILE_COUNT_DEFAULT;
    activeProfileIndex = preferences.getUChar("active_profile", 0) % profileCount;
    // Charger config backlight persistée (env_brightness = LED selon luminosité)
    env_brightness_enabled = preferences.getBool("env_brightness", true);  // true = LED built-in suit la luminosité par défaut
    backlight_enabled = preferences.getBool("backlight_en", true);
    led_brightness = preferences.getUChar("led_brightness", 128);
    led_brightness = max(0, min(255, led_brightness));
    led_color_r = preferences.getUChar("led_cr", 255);
    led_color_g = preferences.getUChar("led_cg", 180);
    led_color_b = preferences.getUChar("led_cb", 50);
    light_source = preferences.getUChar("light_src", 0);
    display_brightness = preferences.getUChar("disp_br", 128);
    String dm = preferences.getString("disp_mode", "data");
    dm.trim(); dm.toLowerCase();
    if (dm != "data" && dm != "image" && dm != "gif") dm = "data";
    strncpy(display_mode_str, dm.c_str(), sizeof(display_mode_str) - 1);
    display_mode_str[sizeof(display_mode_str) - 1] = '\0';
    led_strip_active_count = (uint16_t)preferences.getUInt("led_count", LED_STRIP_DEFAULT_ACTIVE);
    bl_anim_mode = preferences.getUChar("bl_anim", BL_ANIM_SOLID);
    bl_anim_speed_ms = (uint16_t)preferences.getUInt("bl_spd", 2500);
    bl_anim_length = preferences.getUChar("bl_len", 3);
    encoderStep = preferences.getUChar("enc_step", 1);
    if (encoderStep < 1 || encoderStep > 10) encoderStep = 1;
    // (dans l'encadré de boot)

#if USE_ESP32_LIGHT_SENSOR
    pinMode(ESP32_LIGHT_ADC_PIN, INPUT);
    // ADC stable (0..4095 typiquement)
    analogReadResolution(12);
    analogSetPinAttenuation(ESP32_LIGHT_ADC_PIN, ADC_11db);
    // Prime une première lecture
    last_light_level = read_esp32_light_level_0_1023();
    light_filtered = (float)last_light_level;
    // (dans l'encadré de boot)
#endif
    
    // Keymap: defaults puis charger le profil actif
    apply_keymap_defaults();
    loadProfileKeymap(activeProfileIndex);  
    // (dans l'encadré de boot)
    
    // Initialiser UART ATmega (optionnel)
#if ENABLE_ATMEGA_UART
    SerialAtmega.begin(ATMEGA_UART_BAUD, SERIAL_8N1, ATMEGA_UART_RX, ATMEGA_UART_TX);
    delay(100);
    Serial.printf("[UART] ATmega UART initialized TX=%d, RX=%d, %d baud\n",
                  ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD);
#else
    Serial.println("[UART] ATmega UART disabled (ENABLE_ATMEGA_UART=0)");
#endif

#if USE_ESP32_DISPLAY_ST7789
    init_esp32_display_if_needed();
#endif
    
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
    apply_led_strip_runtime_config();
    delay(10);
    update_builtin_led_from_light();
    Serial.printf("[LED] RGB GPIO %d — %u pixels actifs / max %u, anim=%u vitesse=%u ms traînée=%u\n",
                  LED_STRIP_PIN,
                  (unsigned)led_strip_active_count,
                  (unsigned)LED_STRIP_MAX,
                  (unsigned)bl_anim_mode,
                  (unsigned)bl_anim_speed_ms,
                  (unsigned)bl_anim_length);
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
    
#if USE_ESP32_DISPLAY_ST7789 && ESP32_TFT_BOOT_TEST
    // Splash test: confirme que l'ESP32 peut initialiser et dessiner sur l'écran,
    // même si le toggle bypass n'a pas encore été appliqué via la Web UI.
    Serial.println("[TFT] Boot test: init + fill colors");
    tft_hw_pin_test();
    init_esp32_display_if_needed();
    if (esp32DisplayReady) {
        for (int i = 0; i < 6; i++) {
            esp32Tft.fillScreen((i % 2) ? ST77XX_WHITE : rgb565(170, 0, 255));
            esp32Tft.setTextSize(3);
            esp32Tft.setCursor(8, 8);
            esp32Tft.setTextColor((i % 2) ? ST77XX_BLACK : ST77XX_WHITE);
            esp32Tft.print("BOOT");
            delay(300);
        }
    } else {
        Serial.println("[TFT] Boot test: display not ready");
    }
#endif

    // Encadré boot (récapitulatif)
    boot_box_hr();
    boot_box_line("MACROPAD — BOOT SUMMARY");
    boot_box_hr();
    boot_box_kv("USB", "HID ready");
    boot_box_kv("BLE", BLE_AVAILABLE ? "HID ready" : "disabled/error");
    boot_box_kv("Platform", platformDetected.c_str());
    boot_box_kv("LightSource", (light_source == 1) ? "ESP32 ADC" : "ATmega");
    boot_box_kv("Profile", profileName(activeProfileIndex).c_str());
#if ENABLE_ATMEGA_UART
    boot_box_kv("UART", "ATmega enabled");
#else
    boot_box_kv("UART", "ATmega disabled");
#endif
    boot_box_hr();

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
                Serial.println("[BLE] Pour changer d'appareil, deconnectez depuis le telephone/PC");
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
        send_display_data_to_atmega();
        delay(500);
        BLEDevice::getAdvertising()->start();
        Serial.println("[BLE] Restarting advertising after disconnect");
        oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) {
        Serial.println("[BLE] New connection established");
        delay(200);
        send_display_data_to_atmega();
        if (pInputCharacteristic != nullptr) {
            uint8_t empty_report[9] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            pInputCharacteristic->setValue(empty_report, 9);
            pInputCharacteristic->notify();
            Serial.println("[BLE] HID activated");
        }
        oldDeviceConnected = deviceConnected;
    }
    
    int newlinePos;
    while ((newlinePos = bleSerialBuffer.indexOf('\n')) >= 0) {
        String completeMessage = bleSerialBuffer.substring(0, newlinePos);
        bleSerialBuffer = bleSerialBuffer.substring(newlinePos + 1);
        if (completeMessage.length() > 0) {
            processWebMessage(completeMessage);
        }
    }

#if USE_ESP32_LIGHT_SENSOR
    // Lire l’ADC plus souvent que le poll "status", pour que la LED réagisse tout de suite.
    // Envoi au web throttlé via send_light_to_web_if_needed().
    static unsigned long last_adc_read = 0;
    if (light_source == 1 && (now - last_adc_read >= 250)) {
        last_adc_read = now;
        const uint16_t cur = read_esp32_light_level_0_1023();
        // EMA simple: 20% new, 80% old
        light_filtered = light_filtered * 0.80f + (float)cur * 0.20f;
        last_light_level = (uint16_t)clamp_u16((int)(light_filtered + 0.5f), 0, 1023);
        send_light_to_web_if_needed(last_light_level);
    }
#endif
    
    // Luminosité ambiante: USB 30s, BLE 60s (pour LED + écran)
    unsigned long light_interval = deviceConnected ? LIGHT_POLL_INTERVAL_BLE_MS : LIGHT_POLL_INTERVAL_MS;
    if (now - last_light_poll >= light_interval) {
        send_light_level();
    }

    // (Écran contrôlé par ATmega: rien à rafraîchir localement ici)
    
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
        if (settingsObj.containsKey("encoderStep")) {
            int v = settingsObj["encoderStep"].as<int>();
            if (v < 1) v = 1;
            if (v > 10) v = 10;
            encoderStep = (uint8_t)v;
            preferences.putUChar("enc_step", encoderStep);
            Serial.printf("[CONFIG] Encoder step set: %d\n", (int)encoderStep);
        }
        // (Bypass ATmega retiré)
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
    
    // Synchroniser la liste de profils depuis la Web UI (si fournie)
    if (data.containsKey("profiles") && data["profiles"].is<JsonArray>()) {
        JsonArray arr = data["profiles"].as<JsonArray>();
        uint8_t n = (uint8_t)min((size_t)arr.size(), (size_t)10);
        if (n >= 1) {
            profileCount = n;
            preferences.putUChar("profile_count", profileCount);
            for (uint8_t i = 0; i < n; i++) {
                String name = arr[i].as<String>();
                if (name.length() == 0) name = profileName(i);
                // Limite pour affichage écran
                if (name.length() > 15) name = name.substring(0, 15);
                storeProfileName(i, name);
            }
        }
    }

    // Profil actif (si fourni)
    if (data.containsKey("activeProfile")) {
        String ap = data["activeProfile"].as<String>();
        // Matching par nom (liste envoyée par UI)
        bool matched = false;
        for (uint8_t i = 0; i < profileCount; i++) {
            if (getStoredProfileName(i) == ap) {
                activeProfileIndex = i;
                matched = true;
                break;
            }
        }
        // Fallback: "Profil N"
        if (!matched && ap.startsWith("Profil ")) {
            int n = ap.substring(6).toInt(); // "Profil 2" -> 2
            if (n >= 1 && n <= 10) {
                activeProfileIndex = (uint8_t)(n - 1);
                if (activeProfileIndex >= profileCount) {
                    profileCount = activeProfileIndex + 1;
                    preferences.putUChar("profile_count", profileCount);
                }
            }
        }
        setActiveProfile(activeProfileIndex);
    }

    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            KEYMAP[r][c] = "";
        }
    }
    // 0-0 est réservé à PROFILE (le Web UI n'envoie volontairement jamais 0-0)
    KEYMAP[0][0] = "PROFILE";
    
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
                    // Alias Web UI -> firmware (média)
                    if (value == "VOLUME_UP") value = "VOL_UP";
                    else if (value == "VOLUME_DOWN") value = "VOL_DOWN";
                    else if (value == "PLAY_PAUSE") value = "Select";
                    else if (value == "MEDIA_NEXT") value = "Next";
                    else if (value == "MEDIA_PREV") value = "Prev";
                    KEYMAP[row][col] = value;
                }
            }
        }
    }
    // Sécurité: garantir que PROFILE reste assigné à 0-0
    KEYMAP[0][0] = "PROFILE";
    
    // Persister la keymap du profil actif en NVS pour survivre au redémarrage
    saveProfileKeymap(activeProfileIndex);
    
    Serial.println("[WEB] Keymap updated and saved");
    send_display_data_to_atmega();
    send_status_message("Configuration updated");
}

void handle_backlight_message(JsonObject& data) {
    Serial.println("[WEB] Processing backlight message");
    
    if (data.containsKey("colorR") || data.containsKey("colorG") || data.containsKey("colorB")) {
        if (data.containsKey("colorR")) {
            int v = data["colorR"].as<int>();
            led_color_r = (uint8_t)max(0, min(255, v));
        }
        if (data.containsKey("colorG")) {
            int v = data["colorG"].as<int>();
            led_color_g = (uint8_t)max(0, min(255, v));
        }
        if (data.containsKey("colorB")) {
            int v = data["colorB"].as<int>();
            led_color_b = (uint8_t)max(0, min(255, v));
        }
        preferences.putUChar("led_cr", led_color_r);
        preferences.putUChar("led_cg", led_color_g);
        preferences.putUChar("led_cb", led_color_b);
        Serial.printf("[LED] Color RGB (%u,%u,%u)\n", (unsigned)led_color_r, (unsigned)led_color_g, (unsigned)led_color_b);
    }
    if (data["color"].is<JsonObject>()) {
        JsonObject c = data["color"];
        if (c.containsKey("r")) led_color_r = (uint8_t)max(0, min(255, c["r"].as<int>()));
        if (c.containsKey("g")) led_color_g = (uint8_t)max(0, min(255, c["g"].as<int>()));
        if (c.containsKey("b")) led_color_b = (uint8_t)max(0, min(255, c["b"].as<int>()));
        preferences.putUChar("led_cr", led_color_r);
        preferences.putUChar("led_cg", led_color_g);
        preferences.putUChar("led_cb", led_color_b);
        Serial.printf("[LED] Color RGB (%u,%u,%u)\n", (unsigned)led_color_r, (unsigned)led_color_g, (unsigned)led_color_b);
    }
    
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
            update_light_hysteresis_from_levels();
            uint8_t pwm_val = (env_brightness_enabled && !light_is_dark) ? 0 : led_brightness;
            ledcWrite(led_pwm_channel, pwm_val * 1023 / 255);
#endif
#if ENABLE_LED_STRIP
            ledStrip.setBrightness(255);
            update_per_key_leds();
#endif
        }
    }
    
    if (data.containsKey("brightness")) {
        led_brightness = data["brightness"].as<uint8_t>();
        led_brightness = max(0, min(255, led_brightness));
        if (backlight_enabled) {
#if LED_PWM_PIN >= 0
            update_light_hysteresis_from_levels();
            uint8_t pwm_val = (env_brightness_enabled && !light_is_dark) ? 0 : led_brightness;
            ledcWrite(led_pwm_channel, pwm_val * 1023 / 255);
#endif
#if ENABLE_LED_STRIP
            ledStrip.setBrightness(255);
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

#if ENABLE_LED_STRIP
    bool need_apply_strip = false;
    if (data.containsKey("ledCount")) {
        int c = data["ledCount"].as<int>();
        if (c < 1) c = 1;
        if (c > (int)LED_STRIP_MAX) c = (int)LED_STRIP_MAX;
        // Minimum = pixel(s) réservés + 16 touches.
        const int minc = (int)KEY_PIXELS_OFFSET + 16;
        if (c < minc) c = minc;
        led_strip_active_count = (uint16_t)c;
        need_apply_strip = true;
        Serial.printf("[LED] ledCount=%u (max %u)\n", (unsigned)led_strip_active_count, (unsigned)LED_STRIP_MAX);
    }
    if (data.containsKey("animMode")) {
        bl_anim_mode = parse_anim_mode_from_json(data);
        Serial.printf("[LED] animMode=%u\n", (unsigned)bl_anim_mode);
    }
    if (data.containsKey("animSpeed")) {
        int sp = data["animSpeed"].as<int>();
        if (sp < 200) sp = 200;
        if (sp > 12000) sp = 12000;
        bl_anim_speed_ms = (uint16_t)sp;
    }
    if (data.containsKey("animLength")) {
        int Ln = data["animLength"].as<int>();
        if (Ln < 1) Ln = 1;
        if (Ln > 24) Ln = 24;
        bl_anim_length = (uint8_t)Ln;
    }
    if (need_apply_strip) {
        apply_led_strip_runtime_config();
    }
#endif

    if (data.containsKey("lightSource")) {
        String s = data["lightSource"].as<String>();
        s.trim();
        s.toLowerCase();
        uint8_t next = (s == "esp32" || s == "adc") ? 1 : 0;
        light_source = next;
        preferences.putUChar("light_src", light_source);
        Serial.printf("[LIGHT] lightSource=%s\n", light_source ? "esp32" : "atmega");
        // Mettre à jour immédiatement la mesure et l'état LED
        send_light_level();
    }
    
    // Persister backlight pour survie au reboot
    preferences.putBool("backlight_en", backlight_enabled);
    preferences.putUChar("led_brightness", (uint8_t)led_brightness);
#if ENABLE_LED_STRIP
    preferences.putUInt("led_count", led_strip_active_count);
    preferences.putUChar("bl_anim", bl_anim_mode);
    preferences.putUInt("bl_spd", bl_anim_speed_ms);
    preferences.putUChar("bl_len", bl_anim_length);
#endif
    
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
    
    if (data.containsKey("brightness")) {
        int v = data["brightness"].as<int>();
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        display_brightness = (uint8_t)v;
        preferences.putUChar("disp_br", display_brightness);
    }
    if (data.containsKey("mode")) {
        String m = data["mode"].as<String>();
        m.trim();
        m.toLowerCase();
        if (m != "data" && m != "image" && m != "gif") m = "data";
        strncpy(display_mode_str, m.c_str(), sizeof(display_mode_str) - 1);
        display_mode_str[sizeof(display_mode_str) - 1] = '\0';
        preferences.putString("disp_mode", String(display_mode_str));
    }

    // Appliquer sur l'ATmega (écran contrôlé par l'ATmega)
    send_display_data_to_atmega();
    send_status_message("Display config updated");
}

void send_config_to_web() {
    StaticJsonDocument<2560> doc;
    doc["type"] = "config";
    doc["rows"] = NUM_ROWS;
    doc["cols"] = NUM_COLS;
    doc["activeProfile"] = getStoredProfileName(activeProfileIndex);
    doc["outputMode"] = deviceConnected ? "bluetooth" : "usb";
    doc["platform"] = platformDetected;
    doc["bleDeviceName"] = preferences.getString("ble_device_name", "");
    
    JsonObject bl = doc.createNestedObject("backlight");
    bl["enabled"] = backlight_enabled;
    bl["brightness"] = (uint8_t)led_brightness;
    bl["envBrightness"] = env_brightness_enabled;
    bl["colorR"] = led_color_r;
    bl["colorG"] = led_color_g;
    bl["colorB"] = led_color_b;
#if ENABLE_LED_STRIP
    bl["ledCount"] = led_strip_active_count;
    bl["ledMax"] = LED_STRIP_MAX;
    bl["animMode"] = anim_mode_to_string(bl_anim_mode);
    bl["animSpeed"] = bl_anim_speed_ms;
    bl["animLength"] = bl_anim_length;
#endif
    bl["lightSource"] = (light_source == 1) ? "esp32" : "atmega";
    
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
    // Pour rester léger, on expose la liste des profils, mais on ne remplit que le profil actif.
    for (uint8_t i = 0; i < profileCount; i++) {
        JsonObject p = profiles.createNestedObject(getStoredProfileName(i));
        if (i == activeProfileIndex) {
            p["keys"] = keys;
        } else {
            p.createNestedObject("keys");
        }
    }
    
    String output;
    serializeJson(doc, output);
    send_to_web(output);
}

void send_status_message(String message) {
    String json = "{\"type\":\"status\",\"message\":\"" + message + "\"}";
    send_to_web(json);
}

// ==================== SK6812 PER-KEY BACKLIGHT ====================

void apply_led_strip_runtime_config() {
#if ENABLE_LED_STRIP
    // Minimum = pixel(s) réservés + 16 touches.
    // Sinon la dernière touche (souvent "0") se retrouve hors plage si on met ledCount trop bas.
    const uint16_t minc = (uint16_t)KEY_PIXELS_OFFSET + 16;
    if (led_strip_active_count < minc) led_strip_active_count = minc;
    if (led_strip_active_count > (uint16_t)LED_STRIP_MAX) led_strip_active_count = (uint16_t)LED_STRIP_MAX;
    if (bl_anim_mode > BL_ANIM_SPARKLE) bl_anim_mode = BL_ANIM_SOLID;
    if (bl_anim_speed_ms < 200) bl_anim_speed_ms = 200;
    if (bl_anim_speed_ms > 12000) bl_anim_speed_ms = 12000;
    if (bl_anim_length < 1) bl_anim_length = 1;
    if (bl_anim_length > 24) bl_anim_length = 24;
    ledStrip.updateLength(led_strip_active_count);
    ledStrip.begin();
    ledStrip.setBrightness(255);
#endif
}

static uint8_t parse_anim_mode_from_json(JsonObject& o) {
    if (!o.containsKey("animMode")) return BL_ANIM_SOLID;
    JsonVariant v = o["animMode"];
    if (v.is<int>()) {
        int x = v.as<int>();
        if (x >= BL_ANIM_SOLID && x <= BL_ANIM_SPARKLE) return (uint8_t)x;
        return BL_ANIM_SOLID;
    }
    String s = v.as<String>();
    s.trim();
    s.toLowerCase();
    if (s.length() == 0) return BL_ANIM_SOLID;
    if (s == "breathe" || s == "respiration") return BL_ANIM_BREATHE;
    if (s == "rainbow" || s == "arcenciel" || s == "arc-en-ciel") return BL_ANIM_RAINBOW;
    if (s == "chase" || s == "poursuite") return BL_ANIM_CHASE;
    if (s == "scanner" || s == "knight") return BL_ANIM_SCANNER;
    if (s == "sparkle" || s == "etincelles" || s.indexOf("tincel") >= 0) return BL_ANIM_SPARKLE;
    if (s == "solid" || s == "fixe" || s == "static") return BL_ANIM_SOLID;
    return BL_ANIM_SOLID;
}

static const char* anim_mode_to_string(uint8_t m) {
    switch (m) {
        case BL_ANIM_BREATHE: return "breathe";
        case BL_ANIM_RAINBOW: return "rainbow";
        case BL_ANIM_CHASE: return "chase";
        case BL_ANIM_SCANNER: return "scanner";
        case BL_ANIM_SPARKLE: return "sparkle";
        default: return "solid";
    }
}

static void hsvToRgb888(float h, float s, float v, uint8_t* r, uint8_t* g, uint8_t* b) {
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    const float c = v * s;
    const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float rp = 0, gp = 0, bp = 0;
    if (h < 60.0f) {
        rp = c;
        gp = x;
    } else if (h < 120.0f) {
        rp = x;
        gp = c;
    } else if (h < 180.0f) {
        gp = c;
        bp = x;
    } else if (h < 240.0f) {
        gp = x;
        bp = c;
    } else if (h < 300.0f) {
        rp = x;
        bp = c;
    } else {
        rp = c;
        bp = x;
    }
    *r = (uint8_t)min(255, (int)roundf((rp + m) * 255.0f));
    *g = (uint8_t)min(255, (int)roundf((gp + m) * 255.0f));
    *b = (uint8_t)min(255, (int)roundf((bp + m) * 255.0f));
}

// Transition progressive (partagée entre update_builtin_led_from_light et render_backlight_strip_pixels)
#define LED_FADE_STEP 24
#define LED_FADE_INTERVAL_MS 15
static uint8_t led_current_r = 0, led_current_g = 0, led_current_b = 0;
static uint8_t led_target_r = 0, led_target_g = 0, led_target_b = 0;
static unsigned long last_led_fade = 0;

static uint8_t blend_to_target_u8(uint8_t cur, uint8_t tgt, float t) {
    if (cur == tgt) return cur;
    int v = (int)lroundf((float)cur + (float)((int)tgt - (int)cur) * t);
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void render_backlight_strip_pixels() {
#if ENABLE_LED_STRIP
    const uint16_t n = led_strip_active_count;
    if (n == 0) return;
#if LED_STRIP_FIRST_PIXEL_RESERVED
    ledStrip.setPixelColor(BUILTIN_PIXEL_INDEX, 0);
#endif
    const uint16_t first = (uint16_t)KEY_PIXELS_OFFSET;
    if (n <= first) {
        ledStrip.show();
        return;
    }
    const uint16_t nk = (uint16_t)(n - first);
    const uint8_t br = led_current_r;
    const uint8_t bg = led_current_g;
    const uint8_t bb = led_current_b;
    const int mx_env = max((int)br, max((int)bg, (int)bb));
    // En mode environnement : atténuer planchers (queues / minimum respiration) quand le master RVB est bas.
    const float env_anim = env_brightness_enabled ? ((float)mx_env / 255.0f) : 1.0f;
    const uint32_t baseCol = ledStrip.Color(br, bg, bb);
    const unsigned long ms = millis();
    const float spd = (float)max(200, (int)bl_anim_speed_ms);

    switch (bl_anim_mode) {
        case BL_ANIM_SOLID:
            for (uint16_t i = first; i < n; i++) ledStrip.setPixelColor(i, baseCol);
            break;
        case BL_ANIM_BREATHE: {
            float ph = (float)fmod((double)ms / (double)spd, 1.0) * (2.0f * 3.14159265f);
            const float mlo = env_brightness_enabled ? (0.22f * env_anim + 0.02f) : 0.22f;
            float m = mlo + (1.0f - mlo) * (0.5f + 0.5f * sinf(ph));
            uint8_t rr = (uint8_t)min(255, (int)roundf(br * m));
            uint8_t gg = (uint8_t)min(255, (int)roundf(bg * m));
            uint8_t bb2 = (uint8_t)min(255, (int)roundf(bb * m));
            uint32_t c = ledStrip.Color(rr, gg, bb2);
            for (uint16_t i = first; i < n; i++) ledStrip.setPixelColor(i, c);
            break;
        }
        case BL_ANIM_RAINBOW: {
            float t = (float)fmod((double)ms / (double)spd, 1.0) * 360.0f;
            const float step = (nk > 1) ? (360.0f / (float)nk) : 0.0f;
            // Respecter la luminosité courante (mx==0 => noir).
            float v = (float)mx_env / 255.0f;
            for (uint16_t j = 0; j < nk; j++) {
                uint8_t r, g, b2;
                hsvToRgb888(t + step * (float)j, 1.0f, v, &r, &g, &b2);
                ledStrip.setPixelColor((uint16_t)(first + j), ledStrip.Color(r, g, b2));
            }
            break;
        }
        case BL_ANIM_CHASE: {
            float ph = (float)fmod((double)ms / (double)spd, 1.0);
            int pos = (int)(ph * (float)nk);
            if (pos >= (int)nk) pos = (int)nk - 1;
            const int L = max(1, (int)bl_anim_length);
            for (uint16_t j = 0; j < nk; j++) {
                int d = abs((int)j - pos);
                float k = (d < L) ? (1.0f - (float)d / (float)L) : (0.08f * env_anim);
                uint8_t rr = (uint8_t)min(255, (int)roundf(br * k));
                uint8_t gg = (uint8_t)min(255, (int)roundf(bg * k));
                uint8_t bb2 = (uint8_t)min(255, (int)roundf(bb * k));
                ledStrip.setPixelColor((uint16_t)(first + j), ledStrip.Color(rr, gg, bb2));
            }
            break;
        }
        case BL_ANIM_SCANNER: {
            float ph = (float)fmod((double)ms / (double)spd, 1.0);
            float ping = fabsf(ph * 2.0f - 1.0f);
            int pos = (int)roundf(ping * (float)max(1, (int)nk - 1));
            const int L = max(1, (int)bl_anim_length);
            for (uint16_t j = 0; j < nk; j++) {
                int d = abs((int)j - pos);
                float k = (d < L) ? (1.0f - (float)d / (float)L) : (0.08f * env_anim);
                uint8_t rr = (uint8_t)min(255, (int)roundf(br * k));
                uint8_t gg = (uint8_t)min(255, (int)roundf(bg * k));
                uint8_t bb2 = (uint8_t)min(255, (int)roundf(bb * k));
                ledStrip.setPixelColor((uint16_t)(first + j), ledStrip.Color(rr, gg, bb2));
            }
            break;
        }
        case BL_ANIM_SPARKLE: {
            const float sp_bg = 0.18f * env_anim;
            for (uint16_t j = 0; j < nk; j++) {
                uint8_t rr = (uint8_t)min(255, (int)roundf(br * sp_bg));
                uint8_t gg = (uint8_t)min(255, (int)roundf(bg * sp_bg));
                uint8_t bb2 = (uint8_t)min(255, (int)roundf(bb * sp_bg));
                ledStrip.setPixelColor((uint16_t)(first + j), ledStrip.Color(rr, gg, bb2));
            }
            const int sparks = min(4, max(1, (int)nk / 5));
            for (int s = 0; s < sparks; s++) {
                uint16_t j = (uint16_t)random((long)nk);
                ledStrip.setPixelColor((uint16_t)(first + j), baseCol);
            }
            break;
        }
        default:
            for (uint16_t i = first; i < n; i++) ledStrip.setPixelColor(i, baseCol);
            break;
    }
    ledStrip.show();
#endif
}

void apply_keymap_defaults() {
    for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
            KEYMAP[r][c] = String(DEFAULT_KEYMAP[r][c]);
        }
    }
    Serial.println("[KEYMAP] Default keymap applied");
}

int row_col_to_led_index(int row, int col) {
    // Indices strip Adafruit (0 = LED module si FIRST_PIXEL_RESERVED, 1..16 = touches).
    if (row == 2 && col == 3) return (int)(7 + KEY_PIXELS_OFFSET);   // 2-3 = partie de +
    if (row == 4 && col == 1) return (int)(led_strip_active_count - 1);     // "." (dernier pixel touche)
    if (row == 4 && col == 2) return (int)(led_strip_active_count - 1);
    if (row == 4 && col == 3) return (int)(14 + KEY_PIXELS_OFFSET);  // 4-3 = partie de =
    int idx = row * 4 + col;
    idx += (int)KEY_PIXELS_OFFSET;
    if (idx < 0) return -1;
    if (idx >= (int)led_strip_active_count) idx = (int)led_strip_active_count - 1;
    return idx;
}

void update_builtin_led_from_light() {
    // Même si le rétroéclairage est coupé : garder light_is_dark à jour pour l’écran / UART.
    update_light_hysteresis_from_levels();
#if ENABLE_LED_STRIP
    uint8_t tr, tg, tb;
    // "Activer le rétro-éclairage" doit forcer OFF, peu importe le mode environnement.
    if (!backlight_enabled) {
        tr = tg = tb = 0;
    } else if (env_brightness_enabled) {
        // Exiger un délai en noir avant d'allumer (anti-flash).
        // 0 => jamais vu sombre ; sinon timestamp du début de la période "sombre".
        static unsigned long dark_since_ms = 0;

        if (!light_is_dark) {
            dark_since_ms = 0;
        } else if (dark_since_ms == 0) {
            dark_since_ms = millis();
        }

        // Toggle "Selon l'environnement" : même hystérésis que la bande (voir update_light_hysteresis_from_levels).
        const bool dark_stable = (light_is_dark && dark_since_ms != 0 && (millis() - dark_since_ms) >= 3000);
        if (!dark_stable) {
            tr = tg = tb = 0;
        } else {
            uint8_t v = (uint8_t)led_brightness;
            if (v > 0 && v < 20) v = 20;
            uint16_t rr = (uint16_t)led_color_r * v / 255;
            uint16_t gg = (uint16_t)led_color_g * v / 255;
            uint16_t bb = (uint16_t)led_color_b * v / 255;
            tr = (uint8_t)min((int)rr, 255);
            tg = (uint8_t)min((int)gg, 255);
            tb = (uint8_t)min((int)bb, 255);
        }
    } else {
        // Toggle désactivé: luminosité manuelle (backlight_enabled + led_brightness)
        {
            uint8_t v = (uint8_t)led_brightness;
            if (v > 0 && v < 20) v = 20;
            uint16_t rr = (uint16_t)led_color_r * v / 255;
            uint16_t gg = (uint16_t)led_color_g * v / 255;
            uint16_t bb = (uint16_t)led_color_b * v / 255;
            tr = (uint8_t)min((int)rr, 255);
            tg = (uint8_t)min((int)gg, 255);
            tb = (uint8_t)min((int)bb, 255);
        }
    }
    led_target_r = tr;
    led_target_g = tg;
    led_target_b = tb;
    
    // Transition progressive : en mode environnement, interpolation plus rapide et fluide.
    unsigned long now = millis();
    if (env_brightness_enabled) {
        const unsigned long fade_iv = 8UL;
        const float fade_t = 0.38f;
        if (now - last_led_fade >= fade_iv) {
            last_led_fade = now;
            led_current_r = blend_to_target_u8(led_current_r, led_target_r, fade_t);
            led_current_g = blend_to_target_u8(led_current_g, led_target_g, fade_t);
            led_current_b = blend_to_target_u8(led_current_b, led_target_b, fade_t);
        }
    } else if (now - last_led_fade >= LED_FADE_INTERVAL_MS) {
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
    
    ledStrip.setBrightness(255);
    render_backlight_strip_pixels();
#endif

#if LED_PWM_PIN >= 0
    // PWM LED externe (si présent)
    if (!backlight_enabled) {
        ledcWrite(led_pwm_channel, 0);
    } else if (env_brightness_enabled) {
        // Suivre le fondu RVB (même enveloppe que les animations)
        uint8_t mx_pwm = max(led_current_r, max(led_current_g, led_current_b));
        ledcWrite(led_pwm_channel, (uint32_t)mx_pwm * 1023 / 255);
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
    ota_file_size = (size_t)data["size"].as<int>();
    ota_total_chunks = data["chunks"].as<int>();
    String filename = data["filename"].as<String>();
    
    if (ota_in_progress) {
        send_status_message("OTA already in progress");
        return;
    }
    
    if (Update.isRunning()) {
        Update.abort();
    }
    
    if (!Update.begin(ota_file_size, U_FLASH)) {
        send_status_message("OTA begin failed: " + String(Update.errorString()));
        Serial.printf("[OTA] begin failed: %s\n", Update.errorString());
        return;
    }
    
    ota_in_progress = true;
    ota_chunk_count = 0;
    
    Serial.printf("[OTA] Starting update: %s (%u bytes, %d chunks)\n",
                  filename.c_str(), (unsigned)ota_file_size, ota_total_chunks);
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
    if (!ota_in_progress || !Update.isRunning()) {
        send_status_message("OTA: No update in progress");
        return;
    }
    
    String chunk_b64 = data["data"].as<String>();
    bool encoded = data.containsKey("encoded") && data["encoded"].as<bool>();
    
    size_t decoded_len = 0;
    uint8_t decode_buf[OTA_DECODE_BUF_SIZE];
    
    if (encoded) {
        int ret = base64_decode(chunk_b64.c_str(), chunk_b64.length(),
                               decode_buf, sizeof(decode_buf), &decoded_len);
        if (ret != 0 || decoded_len == 0) {
            send_status_message("OTA: Base64 decode error");
            Update.abort();
            ota_in_progress = false;
            return;
        }
    } else {
        if (chunk_b64.length() > sizeof(decode_buf)) {
            send_status_message("OTA: Chunk too large");
            Update.abort();
            ota_in_progress = false;
            return;
        }
        memcpy(decode_buf, chunk_b64.c_str(), chunk_b64.length());
        decoded_len = chunk_b64.length();
    }
    
    size_t written = Update.write(decode_buf, decoded_len);
    if (written != decoded_len) {
        send_status_message("OTA: Write failed");
        Update.abort();
        ota_in_progress = false;
        return;
    }
    
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
    
    Serial.printf("[OTA] Chunk %d/%d (%d%%)\n", ota_chunk_count, ota_total_chunks, progress);
}

void handle_ota_end(JsonObject& data) {
    if (!ota_in_progress || !Update.isRunning()) {
        send_status_message("OTA: No update in progress");
        return;
    }
    
    if (ota_chunk_count < ota_total_chunks) {
        send_status_message("OTA: Incomplete update");
        Update.abort();
        ota_in_progress = false;
        return;
    }
    
    if (!Update.end(true)) {
        send_status_message("OTA failed: " + String(Update.errorString()));
        Serial.printf("[OTA] end failed: %s\n", Update.errorString());
        ota_in_progress = false;
        return;
    }
    
    Serial.println("[OTA] Update completed! Rebooting...");
    send_status_message("OTA: Update completed! Restarting...");
    
    StaticJsonDocument<256> response;
    response["type"] = "ota_status";
    response["status"] = "completed";
    response["message"] = "Update completed, restarting...";
    String output;
    serializeJson(response, output);
    send_to_web(output);
    
    delay(500);
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
    bool debug_web = debug_web_enabled();
    bool interval_elapsed = debug_web && (now - last_light_send_time >= LIGHT_SEND_MIN_INTERVAL_MS);
    if (value_changed || interval_elapsed) {
        last_light_sent_to_web = light_value;
        last_light_send_time = now;
        String msg = "{\"type\":\"light\",\"level\":" + String(light_value) + "}";
        send_to_web(msg);  // USB: Serial | BLE: notify
        if (env_brightness_enabled) {
            bool prev = light_is_dark;
            update_light_hysteresis_from_levels();
            if (prev != light_is_dark) {
                send_last_key_to_atmega();
            }
        }
    }
    // La boucle principale gère déjà le rendu LED; éviter un double rendu ici.
}

void send_atmega_command(uint8_t cmd, uint8_t* payload, int payload_len) {
#if !ENABLE_ATMEGA_UART
    (void)cmd; (void)payload; (void)payload_len;
    return;
#endif
    SerialAtmega.write(cmd);
    if (payload != nullptr && payload_len > 0) {
        SerialAtmega.write(payload, payload_len);
    }
    SerialAtmega.write('\n');
    SerialAtmega.flush();
    // Logs UART (TX) uniquement si debug web activé
    if (debug_web_enabled()) {
        log_line("UART", "TX cmd=0x%02X payload=%dB", cmd, payload_len);
    }
    
    // Log vers la console web (sauf CMD_READ_LIGHT et CMD_SET_LAST_KEY pour éviter flood BLE)
    if (debug_web_enabled() && cmd != CMD_READ_LIGHT && cmd != CMD_SET_LAST_KEY) {
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
#if !ENABLE_ATMEGA_UART
    return;
#endif
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
#if USE_ESP32_LIGHT_SENSOR
    if (light_source == 1) {
        last_light_level = read_esp32_light_level_0_1023();
        send_light_to_web_if_needed(last_light_level);
        return;
    }
#endif
    send_atmega_command(CMD_READ_LIGHT, nullptr, 0);
    send_light_to_web_if_needed(last_light_level);
}

void send_last_key_to_atmega() {
    update_light_hysteresis_from_levels();

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
        back_en = (backlight_enabled && light_is_dark) ? 1 : 0;
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
    update_light_hysteresis_from_levels();
    uint8_t payload[80];
    int pos = 0;
    payload[pos++] = display_brightness;
    const char* mode = display_mode_str;
    uint8_t mode_len = strlen(mode);
    payload[pos++] = mode_len;
    memcpy(&payload[pos], mode, mode_len);
    pos += mode_len;
    String prof = getStoredProfileName(activeProfileIndex);
    uint8_t profile_len = (uint8_t)min((int)prof.length(), 15);
    payload[pos++] = profile_len;
    memcpy(&payload[pos], prof.c_str(), profile_len);
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
        back_en = (backlight_enabled && light_is_dark) ? 1 : 0;
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    } else {
        back_en = backlight_enabled ? 1 : 0;
        back_val = back_en ? (led_brightness & 0xFF) : 0;
    }
    payload[pos++] = back_en & 0xFF;
    payload[pos++] = back_val;
    send_atmega_command(CMD_SET_DISPLAY_DATA, payload, pos);
}
