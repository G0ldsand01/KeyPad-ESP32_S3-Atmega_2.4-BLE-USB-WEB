/*
 * ATmega328P Light Sensor & LED Controller with ST7789 Display
 * 
 * Fonctionnalités:
 * - Lecture du capteur TEMT6000 (luminosité ambiante)
 * - Communication UART avec ESP32 (115200 bauds)
 * - Contrôle PWM de la LED de backlight
 * - Affichage sur écran ST7789 TFT (SPI)
 * 
 * Pins:
 * - TEMT6000: ADC0 (PC0, Pin 23)
 * - LED Backlight: OC0B (PD5, Pin 19) - PWM
 * - UART RX: PD0 (Pin 2) - Reçoit de l'ESP32
 * - UART TX: PD1 (Pin 3) - Envoie à l'ESP32
 * - ST7789 MOSI: PB3 (Pin 17) - SPI Data
 * - ST7789 SCK: PB5 (Pin 19) - SPI Clock
 * - ST7789 CS: PB2 (Pin 16) - Chip Select
 * - ST7789 DC: PB1 (Pin 15) - Data/Command
 * - ST7789 RST: PB0 (Pin 14) - Reset (optionnel, peut être connecté à VCC) 
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <string.h>

// Configuration UART
#define UART_BAUD 115200
#define UART_UBRR (F_CPU / 16 / UART_BAUD - 1)
// F_CPU devrait être défini dans les options du projet (Project Properties > Toolchain > AVR/GNU C++ Compiler > Symbols)
// Si ce n'est pas le cas, décommentez la ligne suivante :
#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// Protocole UART
// Format: [CMD] [DATA...] [\n]
#define CMD_READ_LIGHT 0x01  // Lire la luminosité
#define CMD_SET_LED 0x02     // Définir la luminosité LED (0-255)
#define CMD_GET_LED 0x03     // Obtenir la luminosité LED actuelle
#define CMD_UPDATE_DISPLAY 0x04  // Mettre à jour l'affichage ST7789
#define CMD_SET_DISPLAY_DATA 0x05  // Envoyer les données d'affichage (profil, mode, etc.)
#define CMD_SET_DISPLAY_IMAGE 0x08  // Commencer la réception d'une image RGB565
#define CMD_SET_DISPLAY_IMAGE_CHUNK 0x09  // Recevoir un chunk d'image
#define CMD_SET_ATMEGA_DEBUG 0x0A  // Activer/désactiver le debug UART sur l'ATmega
#define CMD_SET_ATMEGA_LOG_LEVEL 0x0B  // Définir le niveau de log de l'ATmega

// Configuration ST7789
// Pour un écran 1.9" 170x320, utiliser 170 comme hauteur
// Si il y a du bruit en bas, essayer 172 ou ajuster les offsets
#define ST7789_WIDTH 320
#define ST7789_HEIGHT 210
#define ST7789_CS_PORT PORTB
#define ST7789_CS_DDR DDRB
#define ST7789_CS_PIN PB2
#define ST7789_DC_PORT PORTB
#define ST7789_DC_DDR DDRB
#define ST7789_DC_PIN PB1
#define ST7789_RST_PORT PORTB
#define ST7789_RST_DDR DDRB
#define ST7789_RST_PIN PB0

// Commandes ST7789
#define ST7789_NOP 0x00
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_INVON 0x21
#define ST7789_INVOFF 0x20

// Variables globales UART
#define UART_BUFFER_SIZE 256
volatile uint8_t uart_buffer[UART_BUFFER_SIZE];  // Buffer pour recevoir les commandes
volatile uint8_t uart_buffer_index = 0;
volatile uint8_t uart_command = 0;
volatile uint8_t led_brightness = 0;  // 0-255
volatile uint16_t light_level = 0;    // Valeur ADC du TEMT6000 (0-1023)

// Variables pour la réception d'images
#define IMAGE_CHUNK_SIZE 64  // Taille des chunks pour transmission UART (plus grand que I2C)
volatile uint16_t image_expected_size = 0;  // Taille totale de l'image attendue
volatile uint16_t image_received_bytes = 0;  // Nombre de bytes reçus
volatile uint16_t image_chunk_index = 0;  // Index du chunk en cours
volatile uint8_t image_receiving = 0;  // Flag: 1 si on reçoit une image
volatile uint8_t image_chunk_buffer[IMAGE_CHUNK_SIZE];  // Buffer pour stocker temporairement un chunk
volatile uint8_t image_chunk_buffer_index = 0;
// Note: On dessine directement sur l'écran au lieu d'utiliser un buffer (trop grand pour RAM)

// Variables pour les données d'affichage
#define DISPLAY_DATA_BUFFER_SIZE 64
volatile char display_mode[16] = "data";
volatile char display_profile[32] = "Profile 1";
volatile char display_output_mode[16] = "usb";
volatile uint8_t display_keys_count = 0;
volatile uint8_t display_backlight_enabled = 0;
volatile char display_last_key[16] = "";  // Dernière touche appuyée
volatile uint8_t display_backlight_brightness = 0;
volatile char display_custom1[32] = "";
volatile char display_custom2[32] = "";
volatile uint8_t display_brightness = 128;
volatile uint8_t display_data_receiving = 0;
volatile uint8_t display_data_buffer_index = 0;
volatile uint8_t display_initialized = 0;  // Flag: 1 si l'affichage a été initialisé avec Welcome

// Variables pour le debug et logging
volatile uint8_t debug_enabled = 0;  // 0 = désactivé, 1 = activé
volatile uint8_t log_level = 2;  // 0 = none, 1 = error, 2 = info, 3 = debug

// Prototype de uart_send_byte (déclaré avant debug_print pour éviter les erreurs de compilation)
void uart_send_byte(uint8_t data);

// Debug: Utiliser l'UART principal (vers ESP32) pour les messages de débogage
// Tous les messages debug_print() seront envoyés sur l'UART principal (115200 baud)
// L'ESP32 pourra les recevoir et les logger
void debug_init(void) {
    // Pas besoin d'initialiser ici, l'UART principal est déjà initialisé dans uart_init()
    // Cette fonction existe juste pour compatibilité
}

// Envoyer un string sur l'UART principal (vers ESP32)
void debug_print(const char* str) {
    while (*str) {
        uart_send_byte(*str++);
    }
}

// Envoyer une valeur hexadécimale sur l'UART principal
void debug_print_hex(uint8_t val) {
    char hex[] = "0123456789ABCDEF";
    uart_send_byte(hex[(val >> 4) & 0x0F]);
    uart_send_byte(hex[val & 0x0F]);
}

// Envoyer une valeur décimale sur l'UART principal
void debug_print_dec(uint16_t val) {
    char buf[6];
    uint8_t i = 0;
    if (val == 0) {
        uart_send_byte('0');
        return;
    }
    while (val > 0 && i < 5) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        uart_send_byte(buf[--i]);
    }
}

// Macros conditionnelles pour le debug selon le niveau
#define LOG_ERROR(x) do { if (debug_enabled && log_level >= 1) debug_print(x); } while(0)
#define LOG_INFO(x) do { if (debug_enabled && log_level >= 2) debug_print(x); } while(0)
#define LOG_DEBUG(x) do { if (debug_enabled && log_level >= 3) debug_print(x); } while(0)

// Prototypes de fonctions ST7789
void st7789_write_cmd(uint8_t cmd);
void st7789_write_data(uint8_t data);
void st7789_init(void);
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void st7789_fill_screen(uint16_t color);
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void st7789_draw_image_rgb565(uint8_t* imageData, uint16_t imageSize);
void st7789_update_display(void);
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color);
void st7789_draw_text(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg_color);
void processUartCommand(void);
void uart_send_response(uint8_t cmd, uint8_t* data, uint8_t len);
void uart_send_light_ascii(void);
void display_light_level_on_screen(uint16_t value);
void display_simple_info(void);

// Initialiser ADC pour TEMT6000
void adc_init(void) {
    // ADC0 (PC0) comme entrée analogique
    ADMUX = (1 << REFS0);  // Référence AVCC (5V)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // Prescaler 128
}

// Lire la valeur ADC du TEMT6000
uint16_t adc_read(void) {
    ADCSRA |= (1 << ADSC);  // Démarrer conversion
    while (ADCSRA & (1 << ADSC));  // Attendre fin conversion
    return ADC;
}

// Initialiser PWM pour LED (Timer0, OC0B sur PD5)
void pwm_init(void) {
    // Mode PWM Phase Correct, Top = 0xFF
    TCCR0A = (1 << WGM00) | (1 << COM0B1);  // PWM Phase Correct, OC0B non-inversé
    TCCR0B = (1 << CS00);  // Prescaler 1 (pas de division)
    
    // PD5 (OC0B) comme sortie
    DDRD |= (1 << PD5);
    
    OCR0B = 0;  // LED éteinte par défaut
}

// Définir la luminosité LED (0-255)
void set_led_brightness(uint8_t brightness) {
    led_brightness = brightness;
    OCR0B = brightness;  // PWM duty cycle
}

// Initialiser SPI pour ST7789
void spi_init(void) {
    // Configurer SPI en mode maître, vitesse F_CPU/4
    SPCR = (1 << SPE) | (1 << MSTR);  // SPI Enable, Master mode
    SPSR = (1 << SPI2X);  // Double speed
    
    // Pins SPI
    DDRB |= (1 << PB3) | (1 << PB5);  // MOSI et SCK en sortie
    DDRB &= ~(1 << PB4);  // MISO en entrée
    
    // Pins de contrôle ST7789
    ST7789_CS_DDR |= (1 << ST7789_CS_PIN);
    ST7789_DC_DDR |= (1 << ST7789_DC_PIN);
    ST7789_RST_DDR |= (1 << ST7789_RST_PIN);
    
    // CS et RST HIGH par défaut
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
    ST7789_RST_PORT |= (1 << ST7789_RST_PIN);
}

// Envoyer un byte via SPI
void spi_write(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
}

// Envoyer une commande au ST7789
void st7789_write_cmd(uint8_t cmd) {
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);  // CS LOW
    ST7789_DC_PORT &= ~(1 << ST7789_DC_PIN);  // DC LOW (command)
    spi_write(cmd);
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);   // CS HIGH
}

// Envoyer des données au ST7789
void st7789_write_data(uint8_t data) {
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);  // CS LOW
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);    // DC HIGH (data)
    spi_write(data);
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);   // CS HIGH
}

// Envoyer plusieurs bytes de données
void st7789_write_data_multiple(uint8_t* data, uint16_t len) {
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);  // CS LOW
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);    // DC HIGH (data)
    for (uint16_t i = 0; i < len; i++) {
        spi_write(data[i]);
    }
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);   // CS HIGH
}

// Initialiser le ST7789
void st7789_init(void) {
    // Reset hardware
    ST7789_RST_PORT &= ~(1 << ST7789_RST_PIN);
    _delay_ms(20);
    ST7789_RST_PORT |= (1 << ST7789_RST_PIN);
    _delay_ms(20);
    
    // Software reset
    st7789_write_cmd(ST7789_SWRESET);
    _delay_ms(150);
    
    // Sortir du mode sleep
    st7789_write_cmd(ST7789_SLPOUT);
    _delay_ms(150);
    
    // Configuration couleur (RGB565)
    st7789_write_cmd(ST7789_COLMOD);
    st7789_write_data(0x55);  // 16-bit color (RGB565)
    _delay_ms(10);
    
    // Memory access control (orientation)
    // Pour un écran 1.9" 170x320 en mode landscape, connecteur à droite
    st7789_write_cmd(ST7789_MADCTL);
    // Essayer différentes valeurs pour trouver la bonne rotation
    // 0x00 = Normal (portrait, RGB order)
    // 0x60 = 90° rotation (MV=1, landscape, connecteur à gauche)
    // 0xA0 = 270° rotation (MV=1, MY=1, landscape, connecteur à droite)
    // 0xC0 = 180° rotation (MY=1, MX=1)
    st7789_write_data(0xA0);  // Rotation 270° : landscape avec connecteur à droite
    _delay_ms(10);
    
    // Inversion des couleurs - INVON pour que 0x0000 soit noir
    // Si le fond est blanc/rose avec INVOFF, utiliser INVON
    st7789_write_cmd(ST7789_INVON);  // Inversion des couleurs activée
    _delay_ms(10);
    
    // Activer l'affichage
    st7789_write_cmd(ST7789_DISPON);
    _delay_ms(100);  // Délai plus long pour s'assurer que l'écran est prêt
    
    // CRITIQUE: Remplir TOUT l'écran en noir avec fill_screen (plus fiable)
    uint16_t black = 0x0000;  // Noir RGB565
    st7789_fill_screen(black);  // Utiliser fill_screen pour être sûr que tout est noir
    _delay_ms(50);
    
    // Afficher les informations simplifiées
    display_simple_info();
    
    // Marquer que l'affichage a été initialisé
    display_initialized = 1;
    
    debug_print("ST7789 initialized\r\n");
}

// Définir la fenêtre d'affichage
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Pas d'offset - utiliser les coordonnées directement
    st7789_write_cmd(ST7789_CASET);
    st7789_write_data(x0 >> 8);
    st7789_write_data(x0 & 0xFF);
    st7789_write_data(x1 >> 8);
    st7789_write_data(x1 & 0xFF);
    
    st7789_write_cmd(ST7789_RASET);
    st7789_write_data(y0 >> 8);
    st7789_write_data(y0 & 0xFF);
    st7789_write_data(y1 >> 8);
    st7789_write_data(y1 & 0xFF);
    
    st7789_write_cmd(ST7789_RAMWR);
}

// Effacer l'écran avec une couleur
void st7789_fill_screen(uint16_t color) {
    st7789_set_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    
    // RGB565: Format 16-bit RRRRRGGGGGGBBBBB
    // Pour ST7789, tester les deux ordres possibles
    uint8_t color_high = (color >> 8) & 0xFF;
    uint8_t color_low = color & 0xFF;
    
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);
    
    // Dessiner pixel par pixel pour éviter l'overflow
    // 320*170 = 54400 pixels (OK pour uint16_t)
    // Essayer l'ordre normal (high puis low) - certains écrans ST7789 nécessitent cet ordre
    for (uint16_t y = 0; y < ST7789_HEIGHT; y++) {
        for (uint16_t x = 0; x < ST7789_WIDTH; x++) {
            // Ordre normal (high puis low) pour RGB565 - test pour corriger le fond blanc/rose
            spi_write(color_high);
            spi_write(color_low);
        }
    }
    
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
}

// Dessiner un rectangle rempli
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x + w > ST7789_WIDTH) w = ST7789_WIDTH - x;
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;
    
    st7789_set_window(x, y, x + w - 1, y + h - 1);
    
    uint8_t color_high = (color >> 8) & 0xFF;
    uint8_t color_low = color & 0xFF;
    
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);
    
    // Utiliser l'ordre normal (high puis low) pour correspondre à fill_screen
    for (uint16_t i = 0; i < w * h; i++) {
        spi_write(color_high);
        spi_write(color_low);
    }
    
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
}

// Dessiner une image RGB565 complète (240x320)
void st7789_draw_image_rgb565(uint8_t* imageData, uint16_t imageSize) {
    // Vérifier que la taille est correcte (240x320x2 = 153600)
    uint32_t expected_size = (uint32_t)ST7789_WIDTH * ST7789_HEIGHT * 2;
    if (imageSize != expected_size) {
        return;  // Taille invalide
    }
    
    // Définir la fenêtre pour tout l'écran
    st7789_set_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    
    // Envoyer les données pixel par pixel
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);
    
    // Envoyer les données RGB565 (2 bytes par pixel)
    for (uint16_t i = 0; i < imageSize; i++) {
        spi_write(imageData[i]);
    }
    
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
}

// Dessiner une barre de progression horizontale
void st7789_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t value, uint16_t max_value, uint16_t bg_color, uint16_t fg_color) {
    // Fond de la barre
    st7789_fill_rect(x, y, w, h, bg_color);
    
    // Calculer la largeur remplie
    uint16_t filled_width = (uint32_t)w * value / max_value;
    if (filled_width > w) filled_width = w;
    
    // Partie remplie
    if (filled_width > 0) {
        st7789_fill_rect(x, y, filled_width, h, fg_color);
    }
}

// Police bitmap simple 5x7 pour les caractères ASCII
// Chaque caractère est représenté par 5 colonnes de 7 bits
const uint8_t font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Espace (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x00, 0xA0, 0x60, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x59, 0x51, 0x3E}, // @
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

// Dessiner un caractère à la position (x, y)
void st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    // Espace
    if (c == ' ') {
        st7789_fill_rect(x, y, 5, 7, bg_color);
        return;
    }
    
    // Convertir les minuscules en majuscules (la police ne supporte que A-Z)
    if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }
    
    // Remplacer les caractères accentués par leurs équivalents
    if (c == 233 || c == 232 || c == 234 || c == 235) {  // é, è, ê, ë
        c = 'E';
    } else if (c == 224 || c == 225 || c == 226 || c == 227) {  // à, á, â, ã
        c = 'A';
    } else if (c == 249 || c == 250 || c == 251 || c == 252) {  // ù, ú, û, ü
        c = 'U';
    } else if (c == 239 || c == 238 || c == 237 || c == 236) {  // ï, î, í, ì
        c = 'I';
    } else if (c == 242 || c == 243 || c == 244 || c == 245) {  // ò, ó, ô, õ
        c = 'O';
    }
    
    // Vérifier si le caractère est dans la plage ASCII imprimable (32-90 = espace à Z)
    if (c < 32 || c > 90) {
        return;  // Caractère non supporté
    }
    
    uint8_t char_index = c - 32;
    if (char_index >= sizeof(font_5x7) / sizeof(font_5x7[0])) {
        return;  // Index hors limites
    }
    
    // Dessiner le caractère pixel par pixel
    // La police stocke les bits du LSB (bit 0) au MSB (bit 6) pour chaque colonne
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t col_data = font_5x7[char_index][col];
        for (uint8_t row = 0; row < 7; row++) {
            // Vérifier le bit correspondant (bit 0 = ligne du bas, bit 6 = ligne du haut)
            if (col_data & (1 << row)) {
                st7789_fill_rect(x + col, y + row, 1, 1, color);
            } else {
                st7789_fill_rect(x + col, y + row, 1, 1, bg_color);
            }
        }
    }
}

// Dessiner une chaîne de texte
void st7789_draw_text(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg_color) {
    uint16_t x_pos = x;
    while (*text) {
        st7789_draw_char(x_pos, y, *text, color, bg_color);
        x_pos += 6;  // Espacement entre les caractères (5 pixels + 1 pixel d'espace)
        text++;
    }
}

// Mettre à jour l'affichage avec les informations réelles
void st7789_update_display(void) {
    // Si on est en mode image ou gif, ne pas écraser l'image
    if (strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) {
        return;  // L'image est déjà affichée, ne pas l'écraser
    }
    
    uint16_t black = 0xFFFF;  // Noir RGB565
    for (uint8_t i = 0; i < 2; i++) {
        st7789_fill_screen(black);
        _delay_ms(10);
    }
    
    // Afficher le texte plus bas pour qu'il soit bien visible en landscape
    uint16_t text_x = 40;  // 40px de gauche
    uint16_t text_y = 150;  // 150px du top (au lieu de 10)
    uint16_t white = 0xFFFF;  // Blanc RGB565
    
    // Afficher "WELCOME TO MY KEYPAD" (en majuscules car la police ne supporte que A-Z)
    st7789_draw_text(text_x, text_y, "WELCOME TO MY KEYPAD", white, black);
    
    // Afficher l'état de connexion sur la ligne suivante
    uint16_t conn_y = text_y + 12;  // un peu plus bas que le premier texte
    st7789_draw_text(text_x, conn_y, "CONNECTION : ", white, black);
    
    // Déterminer l'état de connexion
    const char* conn_status = "IDLE";
    if (strcmp((char*)display_output_mode, "usb") == 0) {
        conn_status = "USB";
    } else if (strcmp((char*)display_output_mode, "bluetooth") == 0) {
        conn_status = "BLUETOOTH";
    }
    
    // Afficher le statut de connexion (position après "CONNECTION : ")
    uint16_t status_x = text_x + (13 * 6);  // 13 caractères * 6 pixels = 78 pixels
    st7789_draw_text(status_x, conn_y, conn_status, white, black);
    
    return;  // On a affiché le Welcome, on sort (ne pas afficher les autres éléments)
    
    // Code ci-dessous n'est plus utilisé (affichage des barres de progression, etc.)
    // Effacer l'écran avec un fond sombre (gris foncé au lieu de noir pour meilleur contraste)
    uint16_t bg_color = 0x1082;  // Gris foncé RGB565
    st7789_fill_screen(bg_color);
    
    // Couleurs RGB565
    uint16_t color_green = 0x07E0;
    uint16_t color_blue = 0x001F;
    uint16_t color_red = 0xF800;
    uint16_t color_yellow = 0xFFE0;
    uint16_t color_cyan = 0x07FF;
    uint16_t color_magenta = 0xF81F;
    uint16_t color_gray = 0x8410;
    uint16_t color_dark_gray = 0x4208;
    
    // Vérifier le mode d'affichage
    // Si mode = "image" ou "gif", ne rien faire (l'image est déjà affichée)
    // On vérifie seulement si on est en mode "data"
    
    // En-tête avec nom du profil (en haut)
    uint16_t header_y = 5;
    uint16_t header_height = 35;
    st7789_fill_rect(5, header_y, 230, header_height, color_dark_gray);
    // Bordure en bas de l'en-tête
    st7789_fill_rect(5, header_y + header_height - 2, 230, 2, color_blue);
    
    // Indicateur de mode (petit rectangle coloré à gauche)
    uint16_t mode_indicator_color = color_green;
    if (strcmp((char*)display_mode, "image") == 0) mode_indicator_color = color_yellow;
    else if (strcmp((char*)display_mode, "gif") == 0) mode_indicator_color = color_magenta;
    st7789_fill_rect(8, header_y + 8, 8, 20, mode_indicator_color);
    
    // Zone d'information principale (milieu)
    uint16_t info_y = header_y + header_height + 5;
    uint16_t info_height = 200;
    
    // Fond de la zone d'information
    st7789_fill_rect(5, info_y, 230, info_height, color_dark_gray);
    
    // Ligne 1: Mode de sortie (USB/BLE)
    uint16_t line1_y = info_y + 10;
    uint16_t output_color = color_cyan;
    if (strcmp((char*)display_output_mode, "usb") == 0) output_color = color_green;
    else if (strcmp((char*)display_output_mode, "bluetooth") == 0) output_color = color_blue;
    st7789_fill_rect(10, line1_y, 100, 20, output_color);
    
    // Ligne 2: Nombre de touches configurées
    uint16_t line2_y = line1_y + 30;
    uint16_t keys_bar_width = (display_keys_count * 200) / 20;  // 20 touches max
    if (keys_bar_width > 200) keys_bar_width = 200;
    st7789_fill_rect(10, line2_y, 200, 20, color_gray);
    if (keys_bar_width > 0) {
        st7789_fill_rect(10, line2_y, keys_bar_width, 20, color_green);
    }
    
    // Ligne 3: Backlight status
    uint16_t line3_y = line2_y + 30;
    uint16_t backlight_color = display_backlight_enabled ? color_yellow : color_dark_gray;
    uint16_t backlight_width = (display_backlight_brightness * 200) / 255;
    if (backlight_width > 200) backlight_width = 200;
    st7789_fill_rect(10, line3_y, 200, 20, color_gray);
    if (backlight_width > 0) {
        st7789_fill_rect(10, line3_y, backlight_width, 20, backlight_color);
    }
    
    // Ligne 4: Luminosité ambiante
    uint16_t line4_y = line3_y + 30;
    uint16_t light_color = color_green;
    if (light_level < 300) light_color = color_red;
    else if (light_level < 700) light_color = color_yellow;
    uint16_t light_width = (light_level * 200) / 1023;
    if (light_width > 200) light_width = 200;
    st7789_fill_rect(10, line4_y, 200, 20, color_gray);
    if (light_width > 0) {
        st7789_fill_rect(10, line4_y, light_width, 20, light_color);
    }
    
    // Ligne 5: LED Backlight status
    uint16_t line5_y = line4_y + 30;
    uint16_t led_width = (led_brightness * 200) / 255;
    if (led_width > 200) led_width = 200;
    st7789_fill_rect(10, line5_y, 200, 20, color_gray);
    if (led_width > 0) {
        st7789_fill_rect(10, line5_y, led_width, 20, color_blue);
    }
    
    // Ligne 6: Indicateur de statut (carré coloré)
    uint16_t line6_y = line5_y + 30;
    uint16_t status_color = color_green;  // Vert = OK
    st7789_fill_rect(10, line6_y, 30, 30, status_color);
    
    // Pied de page (en bas)
    uint16_t footer_y = ST7789_HEIGHT - 25;
    st7789_fill_rect(5, footer_y, 230, 20, color_dark_gray);
    // Bordure en haut du pied de page
    st7789_fill_rect(5, footer_y, 230, 2, color_blue);
}

// Initialiser UART
void uart_init(void) {
    // Calculer UBRR pour le baud rate
    uint16_t ubrr = (uint16_t)((F_CPU / 16UL / UART_BAUD) - 1);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);
    
    // Activer réception et transmission, interruptions de réception
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
    
    // Format: 8 bits de données, 1 bit de stop, pas de parité
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

int main(void) {
    // CRITIQUE: Désactiver le watchdog timer au démarrage (si activé)
    // Le watchdog peut causer des resets si non désactivé
    MCUSR &= ~(1 << WDRF);  // Clear watchdog reset flag
    wdt_disable();  // Désactiver le watchdog
    
    // Délai initial pour stabilisation de l'alimentation
    _delay_ms(200);  // Délai plus long pour stabilisation au boot
    
    // IMPORTANT: Initialiser l'UART EN PREMIER pour que debug_print() fonctionne
    uart_init();
    _delay_ms(100);  // Attendre que l'UART soit stable
    
    // Initialiser le débogage (utilise maintenant l'UART principal)
    debug_init();
    debug_print("\r\n=== ATmega328P Light Controller ===\r\n");
    debug_print("UART Baud: 115200\r\n");
    debug_print("Boot sequence started...\r\n");
    
    // Initialiser les périphériques
    adc_init();
    debug_print("ADC initialized\r\n");
    
    pwm_init();
    debug_print("PWM initialized\r\n");
    // Forcer le rétro-éclairage à fond pour les tests (LED backlight toujours ON)
    set_led_brightness(255);
    
    spi_init();
    debug_print("SPI initialized\r\n");
    
    // Délai avant initialisation de l'écran pour s'assurer que l'alimentation est stable
    _delay_ms(100);
    st7789_init();
    
    // Premier dessin dans main(): rectangle noir de la taille de l'écran
    st7789_fill_rect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, 0x0000);
    debug_print("ST7789 initialized\r\n");
    
    // Initialiser les valeurs d'affichage par défaut
    strcpy((char*)display_mode, "data");
    strcpy((char*)display_profile, "Profile 1");
    // Par défaut, l'ESP32 utilise le HID BLE (pas USB), donc on affiche BLUETOOTH
    strcpy((char*)display_output_mode, "bluetooth");
    display_keys_count = 0;
    display_backlight_enabled = 1;
    display_backlight_brightness = 255;
    display_brightness = 128;
    
    // Ne pas appeler st7789_update_display() ici - l'affichage Welcome est déjà fait dans st7789_init()
    // On attendra qu'une commande UART demande explicitement une mise à jour
    
    debug_print("UART initialized\r\n");
    
    // Activer interruptions globales
    sei();
    debug_print("Interrupts enabled\r\n");
    debug_print("Ready!\r\n");
    
    // Boucle principale - optimisée pour la réactivité
    while (1) {
        // Traiter les commandes UART en priorité (non-bloquant)
        processUartCommand();
        
        // Lire la luminosité toutes les ~20ms (au lieu de 100ms)
        static uint8_t adc_counter = 0;
        adc_counter++;
        if (adc_counter >= 5) {  // ~100ms (5 * 20ms) pour l'ADC
            adc_counter = 0;
            // Pour l'instant, on lit seulement la luminosité sans toucher au backlight
            light_level = adc_read();
        }
        
        // Rafraîchissement d'affichage de la lumière - toutes les ~200ms
        static uint16_t ui_counter = 0;
        static uint16_t last_shown_light_ui = 0xFFFF;  // Initialiser à une valeur invalide pour forcer le premier affichage
        ui_counter++;
        if (ui_counter >= 10) {  // ~200 ms (10 * 20 ms)
            ui_counter = 0;
            uint16_t diff;
            if (light_level > last_shown_light_ui) {
                diff = light_level - last_shown_light_ui;
            } else {
                diff = last_shown_light_ui - light_level;
            }
            // Mettre à jour l'affichage simplifié si variation >= 5 ou première fois
            if (diff >= 5 || last_shown_light_ui == 0xFFFF) {
                display_simple_info();  // Afficher toutes les infos (inclut la luminosité)
                last_shown_light_ui = light_level;
            }
        }
        
        // Envoyer la luminosité sur l'UART toutes les 1 seconde (~20ms * 50)
        static uint16_t light_send_counter = 0;
        light_send_counter++;
        if (light_send_counter >= 50) {
            light_send_counter = 0;
            uart_send_light_ascii();
        }
        
        // Envoyer un message de debug toutes les 5 secondes (réduit pour moins de spam)
        static uint16_t debug_counter = 0;
        debug_counter++;
        if (debug_counter >= 250) {  // ~5 secondes (250 * 20ms)
            debug_counter = 0;
            debug_print("[LIGHT] Level: ");
            debug_print_dec(light_level);
            debug_print(" (0x");
            debug_print_hex((uint8_t)(light_level >> 8));
            debug_print_hex((uint8_t)(light_level & 0xFF));
            debug_print(")\r\n");
        }
        
        // Délai réduit pour améliorer la réactivité (20ms au lieu de 100ms)
        _delay_ms(20);
    }
    
    return 0;
}

// Envoyer la luminosité actuelle en ASCII lisible sur l'UART (ex: "LIGHT=512\n")
void uart_send_light_ascii(void) {
    uint16_t value = light_level;
    
    // Prefixe
    uart_send_byte('L');
    uart_send_byte('I');
    uart_send_byte('G');
    uart_send_byte('H');
    uart_send_byte('T');
    uart_send_byte('=');
    
    // Conversion décimale (similaire à debug_print_dec mais sur l'UART principal)
    char buf[6];
    uint8_t i = 0;
    if (value == 0) {
        uart_send_byte('0');
    } else {
        while (value > 0 && i < 5) {
            buf[i++] = '0' + (value % 10);
            value /= 10;
        }
        while (i > 0) {
            uart_send_byte(buf[--i]);
        }
    }
    uart_send_byte('\n');
}

    // Afficher la valeur de luminosité sur l'écran ST7789
void display_light_level_on_screen(uint16_t value) {
    // Construire une chaîne "LIGHT: 0123"
    char text[16];
    text[0] = 'L';
    text[1] = 'I';
    text[2] = 'G';
    text[3] = 'H';
    text[4] = 'T';
    text[5] = ':';
    text[6] = ' ';
    
    // Conversion décimale dans text[7..]
    char buf[6];
    uint8_t i = 0;
    uint16_t v = value;
    if (v == 0) {
        text[7] = '0';
        text[8] = '\0';
    } else {
        while (v > 0 && i < 5) {
            buf[i++] = '0' + (v % 10);
            v /= 10;
        }
        uint8_t pos = 7;
        while (i > 0 && pos < sizeof(text) - 1) {
            text[pos++] = buf[--i];
        }
        text[pos] = '\0';
    }
    
    // Couleurs simples
    uint16_t black = 0x0000;
    uint16_t cyan  = 0x07FF;
    
    // Afficher la luminosité juste sous la ligne "CONNECTION : ..."
    // text_y = 40, conn_y = text_y + 12 = 52 → LIGHT vers ~64
    uint16_t x = 20;
    uint16_t y = 150;
    uint16_t w = 300;  // Largeur limitée pour le texte (pas toute la largeur)
    uint16_t h = 100;
    
    // Effacer uniquement la zone du texte (pas toute la largeur)
    st7789_fill_rect(x, y, w, h, black);
    
    // Dessiner le texte "LIGHT: xxxx" en cyan
    st7789_draw_text(x, y, text, cyan, black);
    
    // S'assurer que le reste de l'écran en bas est noir (protection contre le bruit)
    // Effacer de y+h jusqu'en bas de l'écran
    if (y + h < ST7789_HEIGHT) {
        uint16_t clear_y = y + h;
        uint16_t clear_h = ST7789_HEIGHT - clear_y;
        st7789_fill_rect(0, clear_y, ST7789_WIDTH, clear_h, black);
    }
}

// Afficher les informations simplifiées (comme dans l'image)
void display_simple_info(void) {
    static uint8_t first_call = 1;  // Flag pour la première fois
    uint16_t black = 0x0000;
    uint16_t white = 0xFFFF;
    // Couleurs RGB565 - avec INVOFF, les valeurs sont normales
    // Noir = 0x0000, Blanc = 0xFFFF
    // Gris très foncé pour le fond du rectangle intérieur (légèrement plus clair que le noir)
    uint16_t inner_bg = 0x1082;  // Gris très foncé RGB565 (presque noir mais pas 100%)
    // Gris clair pour la bordure et le séparateur
    uint16_t border_gray = 0x8410;  // Gris clair RGB565
    uint16_t separator_gray = 0x8410;  // Gris clair RGB565
    
    // Fond noir pour tout l'écran - seulement la première fois
    if (first_call) {
        st7789_fill_screen(black);
        first_call = 0;
    }
    
    // Rectangle intérieur avec offset de 5px sur les côtés et 30px en haut
    uint16_t offset = 5;
    uint16_t offset_top = 30;  // Offset de 30px depuis le haut
    uint16_t panel_x = offset;
    uint16_t panel_y = offset_top;  // 30px depuis le haut
    uint16_t panel_w = ST7789_WIDTH - (offset * 2);   // 320 - 10 = 310px
    uint16_t panel_h = ST7789_HEIGHT - offset_top - offset;  // Hauteur = écran - offset haut - offset bas
    
    // Dessiner le fond du rectangle intérieur (gris très foncé) - toujours redessiner pour éviter les artefacts
    st7789_fill_rect(panel_x, panel_y, panel_w, panel_h, inner_bg);
    
    // Dessiner les bordures grises à 5px des bords (sur les bords du rectangle intérieur)
    // Bordure supérieure
    st7789_fill_rect(panel_x, panel_y, panel_w, 1, border_gray);
    // Bordure inférieure
    st7789_fill_rect(panel_x, panel_y + panel_h - 1, panel_w, 1, border_gray);
    // Bordure gauche
    st7789_fill_rect(panel_x, panel_y, 1, panel_h, border_gray);
    // Bordure droite
    st7789_fill_rect(panel_x + panel_w - 1, panel_y, 1, panel_h, border_gray);
    
    // Centrer le contenu verticalement et horizontalement dans le rectangle intérieur
    // Calculer la hauteur totale du contenu : profil + séparateur + 6 lignes d'info
    uint16_t line_height = 16;
    uint16_t separator_height = 1;
    uint16_t spacing = 2;
    // Profil (16) + espace (2) + séparateur (1) + espace (2) + 6 lignes (96) = 117px
    uint16_t content_height = line_height + spacing + separator_height + spacing + (6 * line_height);
    uint16_t vertical_offset = 1;  // Offset pour descendre tout le contenu
    uint16_t start_y = panel_y + ((panel_h - content_height) / 2) + vertical_offset;  // Centrer dans le rectangle intérieur + offset
    
    // Marge horizontale pour le texte (centré horizontalement dans le rectangle intérieur)
    uint16_t text_margin = 15;
    uint16_t x = panel_x + text_margin;
    uint16_t y = start_y;
    
    // Profil actuel en haut - convertir en majuscules pour l'affichage
    char profile_text[32];
    uint8_t pos = 0;
    const char* profile_ptr = (char*)display_profile;
    // Si le profil est vide, utiliser "PROFIL 1" par défaut
    if (!profile_ptr || profile_ptr[0] == '\0') {
        profile_ptr = "PROFIL 1";
    }
    while (*profile_ptr && pos < 31) {
        char c = *profile_ptr++;
        // Convertir en majuscule
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        profile_text[pos++] = c;
    }
    profile_text[pos] = '\0';
    // Afficher le profil même s'il est vide (affichera "PROFIL 1")
    st7789_draw_text(x, y, profile_text, white, inner_bg);
    y += line_height + spacing;  // Espace avant le séparateur
    
    // Séparateur centré (largeur de l'écran moins les marges)
    uint16_t separator_width = panel_w - (text_margin * 2);
    st7789_fill_rect(x, y, separator_width, separator_height, separator_gray);
    y += separator_height + spacing;  // Espace après le séparateur
    
    // Mode de connexion - "MODE DE CONNECTION : BLUETOOTH" ou "USB" (tout en majuscules)
    const char* conn_status = "IDLE";
    if (strcmp((char*)display_output_mode, "usb") == 0) {
        conn_status = "USB";
    } else if (strcmp((char*)display_output_mode, "bluetooth") == 0) {
        conn_status = "BLUETOOTH";
    }
    
    // Construire "MODE DE CONNECTION : BLUETOOTH" ou "USB" (tout en majuscules)
    char mode_text[48];
    const char* mode_label = "MODE DE CONNECTION : ";
    pos = 0;  // Réutiliser la variable pos déjà déclarée
    const char* label_ptr = mode_label;
    while (*label_ptr && pos < 47) {
        mode_text[pos++] = *label_ptr++;
    }
    const char* mode_ptr = conn_status;
    while (*mode_ptr && pos < 47) {
        mode_text[pos++] = *mode_ptr++;
    }
    mode_text[pos] = '\0';
    st7789_draw_text(x, y, mode_text, white, inner_bg);
    y += line_height;
    
    // Dernière touche appuyée - "TOUCHE : {touche}" (tout en majuscules)
    char last_key_text[48];
    const char* last_key_label = "TOUCHE : ";
    pos = 0;
    label_ptr = last_key_label;
    while (*label_ptr && pos < 47) {
        last_key_text[pos++] = *label_ptr++;
    }
    const char* last_key_ptr = (char*)display_last_key;
    if (!last_key_ptr || last_key_ptr[0] == '\0') {
        last_key_ptr = "AUCUNE";
    }
    while (*last_key_ptr && pos < 47) {
        char c = *last_key_ptr++;
        // Convertir en majuscule
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        last_key_text[pos++] = c;
    }
    last_key_text[pos] = '\0';
    st7789_draw_text(x, y, last_key_text, white, inner_bg);
    y += line_height;
    
    // Nombre de touches - "TOUCHE CONFIGURE : {touche}/20" (tout en majuscules, sans accent)
    // Les touches viennent de l'ESP32 via display_keys_count
    char keys_text[48];
    const char* keys_label = "TOUCHE CONFIGURE : ";
    pos = 0;
    label_ptr = keys_label;
    while (*label_ptr && pos < 47) {
        keys_text[pos++] = *label_ptr++;
    }
    uint8_t keys_count = display_keys_count;  // Vient de l'ESP32 via UART
    if (keys_count == 0) {
        keys_text[pos++] = '0';
    } else {
        char buf[4];
        uint8_t i = 0;
        while (keys_count > 0 && i < 3) {
            buf[i++] = '0' + (keys_count % 10);
            keys_count /= 10;
        }
        while (i > 0 && pos < 47) {
            keys_text[pos++] = buf[--i];
        }
    }
    keys_text[pos++] = '/';
    keys_text[pos++] = '2';
    keys_text[pos++] = '0';
    keys_text[pos] = '\0';
    st7789_draw_text(x, y, keys_text, white, inner_bg);
    y += line_height;
    
    // Rétro-éclairage - "RETRO-ECLAIRAGE : ON/OFF" (tout en majuscules, sans accent)
    char backlight_text[48];
    const char* backlight_label = "RETRO-ECLAIRAGE : ";
    pos = 0;
    label_ptr = backlight_label;
    while (*label_ptr && pos < 47) {
        backlight_text[pos++] = *label_ptr++;
    }
    if (display_backlight_enabled) {
        backlight_text[pos++] = 'O';
        backlight_text[pos++] = 'N';
    } else {
        backlight_text[pos++] = 'O';
        backlight_text[pos++] = 'F';
        backlight_text[pos++] = 'F';
    }
    backlight_text[pos] = '\0';
    st7789_draw_text(x, y, backlight_text, white, inner_bg);
    y += line_height;
    
    // Niveau de luminosité - "LUMINOSITE : XXXX" (tout en majuscules)
    char light_text[48];
    const char* light_label = "LUMINOSITE : ";
    pos = 0;
    label_ptr = light_label;
    while (*label_ptr && pos < 47) {
        light_text[pos++] = *label_ptr++;
    }
    // Convertir light_level en chaîne (0-1023)
    uint16_t light_val = light_level;
    if (light_val == 0) {
        light_text[pos++] = '0';
    } else {
        char buf[5];
        uint8_t i = 0;
        while (light_val > 0 && i < 4) {
            buf[i++] = '0' + (light_val % 10);
            light_val /= 10;
        }
        while (i > 0 && pos < 47) {
            light_text[pos++] = buf[--i];
        }
    }
    light_text[pos] = '\0';
    st7789_draw_text(x, y, light_text, white, inner_bg);
}

// Fonction pour envoyer une réponse via UART
void uart_send_byte(uint8_t data) {
    while (!(UCSR0A & (1 << UDRE0)));  // Attendre que le buffer de transmission soit vide
    UDR0 = data;
}

void uart_send_response(uint8_t cmd, uint8_t* data, uint8_t len) {
    // Envoyer la commande
    uart_send_byte(cmd);
    // Envoyer les données
    for (uint8_t i = 0; i < len; i++) {
        uart_send_byte(data[i]);
    }
    uart_send_byte('\n');
}

// Traiter une commande UART complète
void processUartCommand() {
    if (uart_buffer_index < 1) return;
    
    uart_command = uart_buffer[0];
    
    LOG_DEBUG("[UART] Command received: 0x");
    debug_print_hex(uart_command);
    debug_print("\r\n");
    
    switch (uart_command) {
        case CMD_READ_LIGHT:
            // Envoyer la luminosité (2 bytes, little-endian)
            {
                uint8_t response[2] = {(uint8_t)(light_level & 0xFF), (uint8_t)((light_level >> 8) & 0xFF)};
                uart_send_response(CMD_READ_LIGHT, response, 2);
            }
            break;
            
        case CMD_GET_LED:
            // Envoyer la luminosité LED
            {
                uint8_t response[1] = {led_brightness};
                uart_send_response(CMD_GET_LED, response, 1);
            }
            break;
            
        case CMD_SET_LED:
            if (uart_buffer_index >= 2) {
                uint8_t brightness = uart_buffer[1];
                LOG_INFO("[UART] Setting LED brightness: ");
                debug_print_dec(brightness);
                debug_print("\r\n");
                set_led_brightness(brightness);
                st7789_update_display();
            }
            break;
            
        case CMD_UPDATE_DISPLAY:
            st7789_update_display();
            break;
            
        case CMD_SET_DISPLAY_DATA:
            // Parser les données d'affichage
            if (uart_buffer_index > 1) {
                uint8_t pos = 1;
                if (pos < uart_buffer_index) {
                    display_brightness = uart_buffer[pos++];
                }
                // Mode
                if (pos < uart_buffer_index) {
                    uint8_t mode_len = uart_buffer[pos++];
                    if (mode_len < 16 && pos + mode_len < uart_buffer_index) {
                        memcpy((void*)display_mode, (const void*)&uart_buffer[pos], mode_len);
                        display_mode[mode_len] = '\0';
                        pos += mode_len;
                    }
                }
                // Profile
                if (pos < uart_buffer_index) {
                    uint8_t profile_len = uart_buffer[pos++];
                    if (profile_len < 32 && pos + profile_len < uart_buffer_index) {
                        memcpy((void*)display_profile, (const void*)&uart_buffer[pos], profile_len);
                        display_profile[profile_len] = '\0';
                        pos += profile_len;
                    }
                }
                // Output mode
                if (pos < uart_buffer_index) {
                    uint8_t output_len = uart_buffer[pos++];
                    if (output_len < 16 && pos + output_len < uart_buffer_index) {
                        memcpy((void*)display_output_mode, (const void*)&uart_buffer[pos], output_len);
                        display_output_mode[output_len] = '\0';
                        pos += output_len;
                    }
                }
                // Keys count
                if (pos < uart_buffer_index) {
                    display_keys_count = uart_buffer[pos++];
                }
                // Last key pressed
                if (pos < uart_buffer_index) {
                    uint8_t last_key_len = uart_buffer[pos++];
                    if (last_key_len < 16 && pos + last_key_len < uart_buffer_index) {
                        memcpy((void*)display_last_key, (const void*)&uart_buffer[pos], last_key_len);
                        display_last_key[last_key_len] = '\0';
                        pos += last_key_len;
                    }
                }
                // Backlight enabled
                if (pos < uart_buffer_index) {
                    display_backlight_enabled = uart_buffer[pos++];
                }
                // Backlight brightness
                if (pos < uart_buffer_index) {
                    display_backlight_brightness = uart_buffer[pos];
                }
                
                // Mettre à jour l'affichage simplifié seulement si on a reçu une vraie configuration
                // Cela évite d'écraser l'affichage au démarrage
                if (strcmp((char*)display_profile, "Profile 1") != 0 || display_keys_count > 0) {
                    display_simple_info();
                }
            }
            break;
            
        case CMD_SET_ATMEGA_DEBUG:
            if (uart_buffer_index >= 2) {
                debug_enabled = uart_buffer[1];
                LOG_INFO("[UART] Debug ");
                if (debug_enabled) {
                    LOG_INFO("enabled\r\n");
                } else {
                    LOG_INFO("disabled\r\n");
                }
            }
            break;
            
        case CMD_SET_ATMEGA_LOG_LEVEL:
            if (uart_buffer_index >= 2) {
                log_level = uart_buffer[1];
                if (log_level > 3) log_level = 3;
                LOG_INFO("[UART] Log level set to: ");
                debug_print_dec(log_level);
                debug_print("\r\n");
            }
            break;
            
        case CMD_SET_DISPLAY_IMAGE:
            if (uart_buffer_index >= 3) {
                image_expected_size = uart_buffer[1] | (uart_buffer[2] << 8);
                image_received_bytes = 0;
                image_chunk_index = 0;
                image_receiving = 1;
                LOG_INFO("[UART] Starting image reception, size: ");
                debug_print_dec(image_expected_size);
                debug_print("\r\n");
            }
            break;
            
        case CMD_SET_DISPLAY_IMAGE_CHUNK:
            if (image_receiving && uart_buffer_index >= 4) {
                uint16_t chunk_idx = uart_buffer[1] | (uart_buffer[2] << 8);
                uint8_t chunk_size = uart_buffer[3];
                
                if (chunk_size > 0 && chunk_size <= IMAGE_CHUNK_SIZE && 
                    (uart_buffer_index - 4) >= chunk_size) {
                    
                    // Calculer la position dans l'image
                    uint16_t byte_offset = chunk_idx * IMAGE_CHUNK_SIZE;
                    uint16_t pixel_offset = byte_offset / 2;
                    uint16_t x = pixel_offset % ST7789_WIDTH;
                    uint16_t y = pixel_offset / ST7789_WIDTH;
                    uint16_t pixels_in_chunk = chunk_size / 2;
                    uint16_t end_x = x + pixels_in_chunk;
                    
                    if (end_x > ST7789_WIDTH) {
                        end_x = ST7789_WIDTH;
                        pixels_in_chunk = ST7789_WIDTH - x;
                    }
                    
                    // Dessiner le chunk sur l'écran
                    st7789_set_window(x, y, end_x - 1, y);
                    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
                    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);
                    for (uint8_t i = 0; i < chunk_size; i++) {
                        spi_write(uart_buffer[4 + i]);
                    }
                    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
                    
                    image_received_bytes += chunk_size;
                    image_chunk_index++;
                    
                    if (image_received_bytes >= image_expected_size) {
                        image_receiving = 0;
                        LOG_INFO("[UART] Image reception complete, ");
                        debug_print_dec(image_received_bytes);
                        debug_print(" bytes\r\n");
                    }
                }
            }
            break;
    }
    
    // Réinitialiser le buffer
    uart_buffer_index = 0;
    uart_command = 0;
}

// Interruption UART (réception)
ISR(USART_RX_vect) {
    uint8_t received = UDR0;
    
    // Si on reçoit un caractère de fin de ligne, traiter la commande
    if (received == '\n' || received == '\r') {
        if (uart_buffer_index > 0) {
            processUartCommand();
        }
        uart_buffer_index = 0;
    } else {
        // Ajouter le byte au buffer
        if (uart_buffer_index < UART_BUFFER_SIZE - 1) {
            uart_buffer[uart_buffer_index++] = received;
        } else {
            // Buffer plein, réinitialiser
            uart_buffer_index = 0;
        }
    }
}