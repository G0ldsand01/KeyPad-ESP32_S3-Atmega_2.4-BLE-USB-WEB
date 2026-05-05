/*
 * ATmega328P Light Sensor & LED Controller with ST7789 Display
 * 
 * Fonctionnalités:
 * - Lecture du capteur TEMT6000 (luminosité ambiante)
 * - Communication UART avec ESP32 (57600 bauds @ 8 MHz, double vitesse U2X)
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
#ifndef F_CPU
#define F_CPU 8000000UL
#endif
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <string.h>

// Configuration UART — 57600 baud @ 8 MHz avec U2X (diviseur 8, erreur raisonnable)
#define UART_BAUD 57600
#define UART_UBRR ((F_CPU / (8UL * UART_BAUD)) - 1UL)

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
#define CMD_SET_LAST_KEY 0x0C  // Envoyer uniquement la dernière touche appuyée
#define CMD_PREPARE_SLEEP 0x0E // ESP32 : avant veille profonde (BL + consignes + SLPIN)
#define CMD_RESUME_FROM_SLEEP 0x0F // ESP32 : réveil rapide — SLPOUT + DISPON + rétro ( données: bl_on, bl_val )

// Capteur TEMT6000: 0 = ADC élevé = clair (LED OFF si >= 500), ADC bas = sombre (LED ON)
#define LIGHT_SENSOR_INVERTED 0

// Configuration ST7789
// Pour un écran 1.9" 170x320, utiliser 170 comme hauteur
// Si il y a du bruit en bas, essayer 172 ou ajuster les offsets
#define ST7789_WIDTH 320
// Beaucoup de modules 1.9" sont 170x320 (landscape = 320x170).
// Une hauteur trop grande peut adresser hors zone visible (aucun pixel affiché).
#define ST7789_HEIGHT 170
// Offsets matériels (très commun sur les dalles 170x320 basées ST7789)
// Si l'écran reste vide, essaie YSTART=0 ou XSTART=35 selon ton module/rotation.
#define ST7789_XSTART 0
#define ST7789_YSTART 35
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
#define ST7789_SLPIN 0x10
#define ST7789_DISPOFF 0x28
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_INVON 0x21
#define ST7789_INVOFF 0x20

// Variables globales UART (SRAM ATmega328P = 2 Ko — éviter 8×256 octets de file)
#define UART_BUFFER_SIZE 128
#define UART_LINE_QUEUE_DEPTH 2
volatile uint8_t uart_buffer[UART_BUFFER_SIZE];  // Ligne copiée ici avant processUartCommand
volatile uint8_t uart_buffer_index = 0;
volatile uint8_t uart_line_queue[UART_LINE_QUEUE_DEPTH][UART_BUFFER_SIZE];
volatile uint8_t uart_line_len[UART_LINE_QUEUE_DEPTH];
volatile uint8_t uart_line_q_in = 0;
volatile uint8_t uart_line_q_out = 0;
volatile uint8_t uart_line_q_count = 0;
static uint8_t uart_rx_line[UART_BUFFER_SIZE];
static volatile uint8_t uart_rx_line_len = 0;
volatile uint8_t uart_command = 0;
volatile uint8_t led_brightness = 0;  // 0-255
volatile uint16_t light_level = 0;    // Valeur ADC du TEMT6000 (0-1023)
volatile uint8_t esp32_backlight_ticks = 0;  // Si > 0: utiliser display_backlight (priorité ESP32)

// Variables pour la réception d'images
#define IMAGE_CHUNK_SIZE 64  // Taille des chunks pour transmission UART (plus grand que I2C)
volatile uint32_t image_expected_size = 0;  // Taille totale RGB565 (320×170×2 = 108800 > uint16_t)
volatile uint32_t image_received_bytes = 0;  // Octets déjà écrits sur le TFT
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
volatile char display_connected_device[32] = "";  // Appareil connecté (USB, Bluetooth, etc.)
volatile uint8_t display_backlight_brightness = 0;
volatile char display_custom1[32] = "";
volatile char display_custom2[32] = "";
volatile uint8_t display_brightness = 128;
volatile uint8_t display_data_receiving = 0;
volatile uint8_t display_data_buffer_index = 0;
volatile uint8_t display_initialized = 0;  // Flag: 1 si l'affichage a été initialisé avec Welcome
/** 1 si CMD SLPIN actif : tout st7789_write_cmd (sauf SLPIN) force SLPOUT + délai avant la commande. */
static uint8_t tft_panel_asleep = 0;
volatile uint8_t display_force_ui_reset = 0;  // 1 après image/gif → data : forcer redessin HUD complet

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

void debug_print_u32(uint32_t val) {
    char buf[11];
    uint8_t i = 0;
    if (val == 0) {
        uart_send_byte('0');
        return;
    }
    while (val > 0 && i < 10) {
        buf[i++] = '0' + (uint8_t)(val % 10u);
        val /= 10u;
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
void display_init_panel(void);
void display_update_partial(uint8_t force_key_device);
void display_draw_image_footer(void);

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

/** Coupe le backlight et invalide la priorité ESP32 — veille panneau (SLPIN / IDLE). */
static void backlight_shutdown_for_panel_sleep(void) {
    display_backlight_enabled = 0;
    display_backlight_brightness = 0;
    esp32_backlight_ticks = 0;
    set_led_brightness(0);
}

/**
 * PWM TFT : ON seulement si la dalle n’est pas en SLPIN.
 * Sinon l’ESP32 (send_display_data / last_key) rallumerait la LED en boucle pendant la veille.
 */
static void apply_display_backlight_from_esp32_consider_sleep(void) {
    if (tft_panel_asleep) {
        esp32_backlight_ticks = 0;
        set_led_brightness(0);
    } else {
        esp32_backlight_ticks = 100;
        set_led_brightness(display_backlight_enabled ? display_backlight_brightness : 0);
    }
}

// Initialiser SPI pour ST7789
void spi_init(void) {
    // IMPORTANT (AVR): le pin SS (PB2) DOIT être configuré en sortie et maintenu HIGH
    // avant d'activer le SPI en mode maître, sinon le matériel peut basculer en mode esclave
    // et certaines transmissions peuvent bloquer (SPIF ne se déclenche pas).
    ST7789_CS_DDR |= (1 << ST7789_CS_PIN);
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);  // CS HIGH (SS high)

    // Pins de contrôle ST7789
    ST7789_DC_DDR |= (1 << ST7789_DC_PIN);
    ST7789_RST_DDR |= (1 << ST7789_RST_PIN);
    ST7789_RST_PORT |= (1 << ST7789_RST_PIN); // RST HIGH par défaut

    // Pins SPI
    DDRB |= (1 << PB3) | (1 << PB5);  // MOSI et SCK en sortie
    DDRB &= ~(1 << PB4);              // MISO en entrée

    // Configurer SPI en mode maître, vitesse F_CPU/2
    SPCR = (1 << SPE) | (1 << MSTR);  // SPI Enable, Master mode
    SPSR = (1 << SPI2X);              // Double speed
}

// Envoyer un byte via SPI
void spi_write(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
}

// Envoyer une commande au ST7789 (réveille le panneau si en SLPIN, sauf pour la commande SLPIN elle-même)
void st7789_write_cmd(uint8_t cmd) {
    if (cmd == ST7789_SLPIN) {
        backlight_shutdown_for_panel_sleep();
        ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
        ST7789_DC_PORT &= ~(1 << ST7789_DC_PIN);
        spi_write(cmd);
        ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
        tft_panel_asleep = 1;
        return;
    }
    if (tft_panel_asleep) {
        ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
        ST7789_DC_PORT &= ~(1 << ST7789_DC_PIN);
        spi_write(ST7789_SLPOUT);
        ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
        _delay_ms(120);
        tft_panel_asleep = 0;
    }
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
    LOG_INFO("[TFT] reset hw\r\n");
    // Reset hardware
    ST7789_RST_PORT &= ~(1 << ST7789_RST_PIN);
    _delay_ms(20);
    ST7789_RST_PORT |= (1 << ST7789_RST_PIN);
    _delay_ms(20);
    
    // Software reset
    LOG_INFO("[TFT] swreset\r\n");
    st7789_write_cmd(ST7789_SWRESET);
    _delay_ms(150);
    
    // Sortir du mode sleep
    LOG_INFO("[TFT] slpout\r\n");
    st7789_write_cmd(ST7789_SLPOUT);
    _delay_ms(150);
    
    // Configuration couleur (RGB565)
    LOG_INFO("[TFT] colmod\r\n");
    st7789_write_cmd(ST7789_COLMOD);
    st7789_write_data(0x55);  // 16-bit color (RGB565)
    _delay_ms(10);
    
    // Memory access control (orientation)
    // Pour un écran 1.9" 170x320 en mode landscape, connecteur à droite
    LOG_INFO("[TFT] madctl\r\n");
    st7789_write_cmd(ST7789_MADCTL);
    // Essayer différentes valeurs pour trouver la bonne rotation
    // 0x00 = Normal (portrait, RGB order)
    // 0x60 = 90° rotation (MV=1, landscape, connecteur à gauche)
    // 0xA0 = 270° rotation (MV=1, MY=1, landscape, connecteur à droite)
    // 0xC0 = 180° rotation (MY=1, MX=1)
    st7789_write_data(0xA0);  // Rotation 270° : landscape avec connecteur à droite
    _delay_ms(10);
    
    // Inversion des couleurs
    // Plusieurs modules ST7789 ont besoin d'INVON pour que le rendu soit correct.
    LOG_INFO("[TFT] invon\r\n");
    st7789_write_cmd(ST7789_INVON);
    _delay_ms(10);
    
    // Activer l'affichage
    LOG_INFO("[TFT] dispon\r\n");
    st7789_write_cmd(ST7789_DISPON);
    _delay_ms(100);  // Délai plus long pour s'assurer que l'écran est prêt
    
    // CRITIQUE: Remplir TOUT l'écran en noir avec fill_screen (plus fiable)
    LOG_INFO("[TFT] fill black\r\n");
    uint16_t black = 0x0000;  // Noir RGB565
    st7789_fill_screen(black);  // Utiliser fill_screen pour être sûr que tout est noir
    _delay_ms(50);
    
    // Afficher les informations simplifiées
    LOG_INFO("[TFT] ui\r\n");
    display_simple_info();
    
    // Marquer que l'affichage a été initialisé
    display_initialized = 1;
    
    debug_print("ST7789 initialized\r\n");
}

// Définir la fenêtre d'affichage
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // IMPORTANT: beaucoup de ST7789 n'aiment pas que CS remonte entre les bytes
    // des paramètres CASET/RASET. On envoie donc chaque bloc en "burst" (CS bas).

    // Appliquer offsets (certains panneaux ont une zone visible décalée dans la RAM)
    x0 += ST7789_XSTART;
    x1 += ST7789_XSTART;
    y0 += ST7789_YSTART;
    y1 += ST7789_YSTART;

    st7789_write_cmd(ST7789_CASET);
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);  // CS LOW
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);   // DC HIGH (data)
    spi_write((uint8_t)(x0 >> 8));
    spi_write((uint8_t)(x0 & 0xFF));
    spi_write((uint8_t)(x1 >> 8));
    spi_write((uint8_t)(x1 & 0xFF));
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);   // CS HIGH

    st7789_write_cmd(ST7789_RASET);
    ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);  // CS LOW
    ST7789_DC_PORT |= (1 << ST7789_DC_PIN);   // DC HIGH (data)
    spi_write((uint8_t)(y0 >> 8));
    spi_write((uint8_t)(y0 & 0xFF));
    spi_write((uint8_t)(y1 >> 8));
    spi_write((uint8_t)(y1 & 0xFF));
    ST7789_CS_PORT |= (1 << ST7789_CS_PIN);   // CS HIGH

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
const uint8_t font_5x7[][5] PROGMEM = {
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
        uint8_t col_data = pgm_read_byte(&font_5x7[char_index][col]);
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

/* Police x2 pour en-tête (lisibilité, remplit l'espace sans bitmap lourd) */
static void ui_draw_char_x2(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    if (c == ' ') {
        st7789_fill_rect(x, y, 10, 14, bg_color);
        return;
    }
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c < 32 || c > 90) {
        return;
    }
    uint8_t char_index = (uint8_t)(c - 32);
    if (char_index >= (uint8_t)(sizeof(font_5x7) / sizeof(font_5x7[0]))) {
        return;
    }
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t col_data = pgm_read_byte(&font_5x7[char_index][col]);
        for (uint8_t row = 0; row < 7; row++) {
            uint16_t px = (col_data & (uint8_t)(1u << row)) ? color : bg_color;
            st7789_fill_rect(x + (uint16_t)col * 2u, y + (uint16_t)row * 2u, 2, 2, px);
        }
    }
}

static void ui_draw_text_x2(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg_color) {
    uint16_t x_pos = x;
    while (*text) {
        ui_draw_char_x2(x_pos, y, *text, color, bg_color);
        x_pos += 12;
        text++;
    }
}

// Mettre à jour l'affichage avec les informations réelles
void st7789_update_display(void) {
    if (image_receiving) {
        return;
    }
    if (strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) {
        display_draw_image_footer();
        return;
    }
    display_force_ui_reset = 1;
    display_update_partial(1);
}

// Initialiser UART
void uart_init(void) {
    UCSR0A |= (1 << U2X0);
    UBRR0H = (uint8_t)(UART_UBRR >> 8);
    UBRR0L = (uint8_t)(UART_UBRR & 0xFF);
    
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
    debug_print("UART Baud: 57600 (U2X)\r\n");
    debug_print("Boot sequence started...\r\n");
    
    // Initialiser les périphériques
    adc_init();
    debug_print("ADC initialized\r\n");
    
    pwm_init();
    debug_print("PWM initialized\r\n");
    // LED backlight: contrôlée par light_level (>= 500 = ON)
    
    spi_init();
    debug_print("SPI initialized\r\n");
    
    // Délai avant initialisation de l'écran pour s'assurer que l'alimentation est stable
    _delay_ms(100);
    st7789_init();
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
        // Traiter toutes les lignes UART mises en file par l'ISR (évite perte entre paquets image)
        for (;;) {
            uint8_t cnt;
            cli();
            cnt = uart_line_q_count;
            sei();
            if (cnt == 0) {
                break;
            }
            cli();
            uint8_t out = uart_line_q_out;
            uint8_t len = uart_line_len[out];
            uart_line_q_out = (uint8_t)((uart_line_q_out + 1u) % UART_LINE_QUEUE_DEPTH);
            uart_line_q_count--;
            sei();
            if (len == 0 || len >= UART_BUFFER_SIZE) {
                continue;
            }
            memcpy((void*)uart_buffer, (const void*)uart_line_queue[out], len);
            uart_buffer_index = len;
            processUartCommand();
        }
        
        // Économie : dalle en SLPIN → pas d’ADC/UI périodiques, MCU en veille IDLE (UART RX réveille).
        if (!tft_panel_asleep) {
            // Lire la luminosité toutes les ~20ms (au lieu de 100ms)
            static uint8_t adc_counter = 0;
            adc_counter++;
            if (adc_counter >= 5) {  // ~100ms (5 * 20ms) pour l'ADC
                adc_counter = 0;
                light_level = adc_read();
                if (esp32_backlight_ticks > 0) {
                    esp32_backlight_ticks--;
                }
                set_led_brightness(display_backlight_enabled ? display_backlight_brightness : 0);
            }

            static uint16_t ui_counter = 0;
            static uint16_t last_shown_light_ui = 0xFFFF;
            ui_counter++;
            if (ui_counter >= 10) {
                ui_counter = 0;
                uint16_t diff;
                if (light_level > last_shown_light_ui) {
                    diff = light_level - last_shown_light_ui;
                } else {
                    diff = last_shown_light_ui - light_level;
                }
                if (diff >= 5 || last_shown_light_ui == 0xFFFF) {
                    display_simple_info();
                    last_shown_light_ui = light_level;
                }
            }

            static uint16_t debug_counter = 0;
            debug_counter++;
            if (debug_counter >= 250) {
                debug_counter = 0;
                debug_print("[LIGHT] Level: ");
                debug_print_dec(light_level);
                debug_print(" (0x");
                debug_print_hex((uint8_t)(light_level >> 8));
                debug_print_hex((uint8_t)(light_level & 0xFF));
                debug_print(")\r\n");
            }

            _delay_ms(20);
        } else {
            uint8_t qcnt;
            cli();
            qcnt = uart_line_q_count;
            sei();
            if (qcnt == 0) {
                /* Dalle en veille : garantir PWM backlight à 0 (boucle IDLE sans passage ADC). */
                set_led_brightness(0);
                set_sleep_mode(SLEEP_MODE_IDLE);
                sleep_enable();
                sei();
                sleep_cpu();
                sleep_disable();
            } else {
                _delay_ms(1);
            }
        }
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
    uint16_t fg_gray = 0x8430;  // Même neutre que UI_TEXT_HI
    
    // Afficher la luminosité juste sous la ligne "CONNECTION : ..."
    // text_y = 40, conn_y = text_y + 12 = 52 → LIGHT vers ~64
    uint16_t x = 20;
    uint16_t y = 150;
    uint16_t w = 300;  // Largeur limitée pour le texte (pas toute la largeur)
    uint16_t h = 100;
    
    // Effacer uniquement la zone du texte (pas toute la largeur)
    st7789_fill_rect(x, y, w, h, black);
    
    st7789_draw_text(x, y, text, fg_gray, black);
    
    // S'assurer que le reste de l'écran en bas est noir (protection contre le bruit)
    // Effacer de y+h jusqu'en bas de l'écran
    if (y + h < ST7789_HEIGHT) {
        uint16_t clear_y = y + h;
        uint16_t clear_h = ST7789_HEIGHT - clear_y;
        st7789_fill_rect(0, clear_y, ST7789_WIDTH, clear_h, black);
    }
}

/* ─── UI : style sombre + accents or / USB vert ─── */
#define UI_BG           0x0000
#define UI_TEXT_HI      0xFFFF  // valeurs en blanc
#define UI_TEXT_DIM     0xFEC0  // libellés type « or »
#define UI_ACCENT       0xFD20  // barres / % ambiant
#define UI_DIVIDER      0xBD20  // séparateur plus visible sur fond noir
#define UI_VAL_USB      0x07F0
#define UI_VAL_BLE      0x4DDF
#define UI_VAL_IDLE     0xBDF7

#define UI_CONTENT_Y    38
#define UI_ROW_GAP      12
#define UI_ROW_LRG      18
#define UI_ROW_AMB      16
#define UI_LBL_X        10
#define UI_VAL_X        118
#define UI_GUTTER_X     6
#define UI_BAR_X        10
#define UI_BAR_W        300
#define UI_KEYS_CAP     17

/* Bande réservée en bas quand une image plein écran est affichée (alignée avec ESP map_packed565). */
#define UI_IMAGE_FOOTER_Y0  76
#define UI_FOOTER_ROW_H     10

static void ui_hrule(uint16_t y) {
    if (y >= ST7789_HEIGHT) return;
    st7789_fill_rect(8, y, ST7789_WIDTH - 16, 1, UI_DIVIDER);
}

static void fmt_pct(char* dst, uint8_t p) {
    if (p >= 100) {
        dst[0] = '1';
        dst[1] = '0';
        dst[2] = '0';
        dst[3] = '\0';
    } else if (p >= 10) {
        dst[0] = (char)('0' + (p / 10));
        dst[1] = (char)('0' + (p % 10));
        dst[2] = '\0';
    } else {
        dst[0] = (char)('0' + p);
        dst[1] = '\0';
    }
}

static void fmt_u8(char* dst, uint8_t v) {
    if (v >= 100) {
        dst[0] = '0' + (v / 100);
        dst[1] = '0' + ((v / 10) % 10);
        dst[2] = '0' + (v % 10);
        dst[3] = '\0';
    } else if (v >= 10) {
        dst[0] = '0' + (v / 10);
        dst[1] = '0' + (v % 10);
        dst[2] = '\0';
    } else {
        dst[0] = '0' + v;
        dst[1] = '\0';
    }
}

static void __attribute__((unused)) fmt_u16(char* dst, uint16_t v) {
    char tmp[6];
    uint8_t i = 0;
    if (v == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        return;
    }
    while (v > 0 && i < 5) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    uint8_t j = 0;
    while (i > 0) {
        dst[j++] = tmp[--i];
    }
    dst[j] = '\0';
}

static void ui_clear_band(uint16_t y, uint16_t h) {
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;
    st7789_fill_rect(0, y, ST7789_WIDTH, h, UI_BG);
}

static void ui_gutter_line(uint16_t y, uint16_t h) {
    if (h < 1) return;
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;
    st7789_fill_rect(UI_GUTTER_X, y, 1, h, UI_DIVIDER);
}

static void ui_row_two_col_color(uint16_t y, const char* lbl, const char* val, uint16_t val_color) {
    ui_clear_band(y, UI_ROW_GAP);
    ui_gutter_line(y, UI_ROW_GAP);
    st7789_draw_text(UI_LBL_X, y + 3, lbl, UI_TEXT_DIM, UI_BG);
    st7789_draw_text(UI_VAL_X, y + 3, val, val_color, UI_BG);
}

static void ui_row_two_col(uint16_t y, const char* lbl, const char* val) {
    ui_row_two_col_color(y, lbl, val, UI_TEXT_HI);
}

static void ui_row_keys(uint16_t y, uint8_t kc) {
    char nbuf[8];
    char line2[22];
    uint8_t pct = (uint8_t)(((uint16_t)kc * 100u) / (uint16_t)UI_KEYS_CAP);
    ui_clear_band(y, UI_ROW_LRG);
    ui_gutter_line(y, UI_ROW_LRG);
    st7789_draw_text(UI_LBL_X, y + 2, "MAPPING", UI_TEXT_DIM, UI_BG);
    nbuf[0] = '\0';
    fmt_u8(nbuf, kc);
    uint8_t p = 0;
    const char* q = nbuf;
    while (*q && p < sizeof(line2) - 8) line2[p++] = *q++;
    line2[p++] = '/';
    fmt_u8(nbuf, (uint8_t)UI_KEYS_CAP);
    q = nbuf;
    while (*q && p < sizeof(line2) - 6) line2[p++] = *q++;
    line2[p++] = ' ';
    line2[p++] = '(';
    char pb[6];
    fmt_pct(pb, pct);
    q = pb;
    while (*q && p < sizeof(line2) - 2) line2[p++] = *q++;
    line2[p++] = '%';
    line2[p++] = ')';
    line2[p] = '\0';
    st7789_draw_text(UI_VAL_X, y + 2, line2, UI_TEXT_HI, UI_BG);
    st7789_draw_progress_bar(UI_BAR_X, y + 14, UI_BAR_W, 3, kc, UI_KEYS_CAP, UI_DIVIDER, UI_ACCENT);
}

static void ui_row_ambient(uint16_t y, uint16_t lv) {
    char pctstr[8];
    uint16_t capped = lv;
    if (capped > 1023) capped = 1023;
    uint8_t pct = (uint8_t)(((uint32_t)capped * 100u) / 1023u);

    ui_clear_band(y, UI_ROW_AMB);
    ui_gutter_line(y, UI_ROW_AMB);
    st7789_draw_text(UI_LBL_X, y + 2, "LUM AMB", UI_TEXT_DIM, UI_BG);

    uint8_t pi = 0;
    fmt_pct(pctstr, pct);
    while (pctstr[pi] && pi < sizeof(pctstr) - 2) pi++;
    pctstr[pi++] = '%';
    pctstr[pi] = '\0';
    st7789_draw_text(UI_VAL_X, y + 2, pctstr, UI_ACCENT, UI_BG);

    st7789_draw_progress_bar(UI_BAR_X, y + 11, UI_BAR_W, 2, capped, 1023u, UI_DIVIDER, UI_ACCENT);
}

static void ui_row_led(uint16_t y, uint8_t be, uint8_t pwm) {
    char pctstr[8];
    ui_clear_band(y, UI_ROW_GAP);
    ui_gutter_line(y, UI_ROW_GAP);
    st7789_draw_text(UI_LBL_X, y + 3, "ECRAN", UI_TEXT_DIM, UI_BG);

    if (be) {
        uint8_t bpct = (uint8_t)(((uint32_t)pwm * 100u + 127u) / 255u);
        if (bpct > 100) bpct = 100;
        st7789_draw_text(124, y + 3, "ON", UI_ACCENT, UI_BG);
        uint8_t pi = 0;
        fmt_pct(pctstr, bpct);
        while (pctstr[pi] && pi < sizeof(pctstr) - 2) pi++;
        pctstr[pi++] = '%';
        pctstr[pi] = '\0';
        st7789_draw_text(196, y + 3, pctstr, UI_TEXT_HI, UI_BG);
    } else {
        st7789_draw_text(96, y + 3, "OFF", UI_TEXT_DIM, UI_BG);
        st7789_draw_text(196, y + 3, "0%", UI_TEXT_DIM, UI_BG);
    }
}

// En-tête FlexPad + séparation, puis rangées de statut (profil … L.2)
void display_init_panel(void) {
    st7789_fill_screen(UI_BG);
    ui_draw_text_x2(10, 2, "FlexPad", UI_TEXT_DIM, UI_BG);
    ui_hrule(35);
}

// Helper: mettre une chaîne en majuscules dans out (max len-1 chars + null)
static void to_upper_str(const char* in, char* out, uint8_t len) {
    uint8_t i = 0;
    while (i < len - 1 && in && *in) {
        char c = *in++;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        out[i++] = c;
    }
    out[i] = '\0';
}

static void footer_paint_img_row(uint16_t* py, const char* lbl, const char* val, uint16_t valcol) {
    st7789_fill_rect(0, *py, ST7789_WIDTH, UI_FOOTER_ROW_H, UI_BG);
    ui_gutter_line(*py, UI_FOOTER_ROW_H);
    st7789_draw_text(UI_LBL_X, (uint16_t)(*py + 1), lbl, UI_TEXT_DIM, UI_BG);
    st7789_draw_text(UI_VAL_X, (uint16_t)(*py + 1), val, valcol, UI_BG);
    *py = (uint16_t)(*py + UI_FOOTER_ROW_H);
}

static void footer_keys_compact_line(char* line2, uint8_t line2_sz, uint8_t kc) {
    char nbuf[8];
    uint8_t pct = (uint8_t)(((uint16_t)kc * 100u) / (uint16_t)UI_KEYS_CAP);
    uint8_t p = 0;
    nbuf[0] = '\0';
    fmt_u8(nbuf, kc);
    const char* q = nbuf;
    while (*q && p < line2_sz - 8) line2[p++] = *q++;
    line2[p++] = '/';
    fmt_u8(nbuf, (uint8_t)UI_KEYS_CAP);
    q = nbuf;
    while (*q && p < line2_sz - 6) line2[p++] = *q++;
    line2[p++] = ' ';
    line2[p++] = '(';
    char pb[6];
    fmt_pct(pb, pct);
    q = pb;
    while (*q && p < line2_sz - 2) line2[p++] = *q++;
    line2[p++] = '%';
    line2[p++] = ')';
    line2[p] = '\0';
}

/* Statut complet en bande basse par-dessus une image (zone non peinte par l'ESP32). */
void display_draw_image_footer(void) {
    char valbuf[17];
    char line2[22];
    uint16_t y = UI_IMAGE_FOOTER_Y0;
    if (UI_IMAGE_FOOTER_Y0 + 8 >= ST7789_HEIGHT) {
        return;
    }
    st7789_fill_rect(0, UI_IMAGE_FOOTER_Y0, ST7789_WIDTH, (uint16_t)(ST7789_HEIGHT - UI_IMAGE_FOOTER_Y0), UI_BG);
    ui_hrule(UI_IMAGE_FOOTER_Y0);
    y = (uint16_t)(UI_IMAGE_FOOTER_Y0 + 2);

    const char* profile_ptr = (char*)display_profile;
    if (!profile_ptr || !profile_ptr[0]) profile_ptr = "PROFIL 1";
    to_upper_str(profile_ptr, valbuf, sizeof(valbuf));
    footer_paint_img_row(&y, "PROFIL", valbuf, UI_TEXT_HI);

    const char* conn_status = "IDLE";
    uint16_t conn_col = UI_VAL_IDLE;
    if (strcmp((char*)display_output_mode, "usb") == 0) {
        conn_status = "USB";
        conn_col = UI_VAL_USB;
    } else if (strcmp((char*)display_output_mode, "bluetooth") == 0) {
        conn_status = "BLUETOOTH";
        conn_col = UI_VAL_BLE;
    }
    footer_paint_img_row(&y, "LIAISON", conn_status, conn_col);

    const char* device_ptr = (char*)display_connected_device;
    if (!device_ptr || !device_ptr[0]) {
        device_ptr = (strcmp((char*)display_output_mode, "bluetooth") == 0) ? "SANS FIL" : "FILAIRE";
    }
    to_upper_str(device_ptr, valbuf, sizeof(valbuf));
    footer_paint_img_row(&y, "HOTE", valbuf, UI_TEXT_HI);

    const char* last_key_ptr = (char*)display_last_key;
    const char* last_key_display = (!last_key_ptr || !last_key_ptr[0]) ? "-" : last_key_ptr;
    to_upper_str(last_key_display, valbuf, sizeof(valbuf));
    footer_paint_img_row(&y, "TOUCHE", valbuf, UI_TEXT_HI);

    footer_keys_compact_line(line2, sizeof(line2), display_keys_count);
    footer_paint_img_row(&y, "MAPPING", line2, UI_TEXT_HI);

    uint16_t lv = light_level;
    uint16_t capped = lv > 1023 ? 1023 : lv;
    uint8_t pct = (uint8_t)(((uint32_t)capped * 100u) / 1023u);
    char pctstr[8];
    uint8_t pi = 0;
    fmt_pct(pctstr, pct);
    while (pctstr[pi] && pi < sizeof(pctstr) - 2) pi++;
    pctstr[pi++] = '%';
    pctstr[pi] = '\0';
    footer_paint_img_row(&y, "LUM AMB", pctstr, UI_ACCENT);

    uint8_t be = display_backlight_enabled ? 1 : 0;
    uint8_t pwm = display_backlight_brightness;
    char ledline[16];
    if (be) {
        uint8_t bpct = (uint8_t)(((uint32_t)pwm * 100u + 127u) / 255u);
        if (bpct > 100) bpct = 100;
        char pctled[6];
        fmt_pct(pctled, bpct);
        uint8_t j = 0;
        ledline[j++] = 'O';
        ledline[j++] = 'N';
        ledline[j++] = ' ';
        const char* q = pctled;
        while (*q && j < sizeof(ledline) - 2) ledline[j++] = *q++;
        ledline[j++] = '%';
        ledline[j] = '\0';
    } else {
        ledline[0] = 'O';
        ledline[1] = 'F';
        ledline[2] = 'F';
        ledline[3] = '\0';
    }
    footer_paint_img_row(&y, "ECRAN", ledline, UI_TEXT_HI);

    const char* c1 = (char*)display_custom1;
    const char* c2 = (char*)display_custom2;
    if (c1 && c1[0]) {
        to_upper_str(c1, valbuf, sizeof(valbuf));
        footer_paint_img_row(&y, "L.1", valbuf, UI_TEXT_HI);
    }
    if (c2 && c2[0]) {
        to_upper_str(c2, valbuf, sizeof(valbuf));
        footer_paint_img_row(&y, "L.2", valbuf, UI_TEXT_HI);
    }
}

// Mise à jour partielle: ne redessine que les zones dont la valeur a changé
// force_key_device=1: force toujours la mise à jour des zones dernière touche et appareil
void display_update_partial(uint8_t force_key_device) {
    static uint8_t panel_drawn = 0;
    static char prev_profile[32] = "";
    static char prev_output_mode[16] = "";
    static char prev_connected_device[32] = "";
    static char prev_last_key[16] = "";
    static uint8_t prev_keys_count = 255;
    static uint8_t prev_backlight_enabled = 255;
    static uint8_t prev_backlight_pwm = 255;
    static uint16_t prev_light_level = 0xFFFF;
    static char prev_c1[32] = "";
    static char prev_c2[32] = "";

    if (image_receiving) {
        return;
    }
    if (strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) {
        return;
    }

    if (display_force_ui_reset) {
        panel_drawn = 0;
        prev_profile[0] = '\0';
        prev_output_mode[0] = '\0';
        prev_connected_device[0] = '\0';
        prev_last_key[0] = '\0';
        prev_keys_count = 255;
        prev_backlight_enabled = 255;
        prev_backlight_pwm = 255;
        prev_light_level = 0xFFFF;
        prev_c1[0] = '\0';
        prev_c2[0] = '\0';
        display_force_ui_reset = 0;
    }

    if (!panel_drawn) {
        display_init_panel();
        panel_drawn = 1;
    }

    const uint16_t y0 = UI_CONTENT_Y;
    const uint16_t y_profile = y0;
    const uint16_t y_lia = y_profile + UI_ROW_GAP;
    const uint16_t y_host = y_lia + UI_ROW_GAP;
    const uint16_t y_key = y_host + UI_ROW_GAP;
    const uint16_t y_map = y_key + UI_ROW_GAP;
    const uint16_t y_amb = y_map + UI_ROW_LRG;
    const uint16_t y_led = y_amb + UI_ROW_AMB;

    char valbuf[17];
    const char* profile_ptr = (char*)display_profile;
    if (!profile_ptr || !profile_ptr[0]) profile_ptr = "PROFIL 1";
    if (strcmp(profile_ptr, prev_profile) != 0) {
        strncpy((char*)prev_profile, profile_ptr, 31);
        prev_profile[31] = '\0';
        to_upper_str(profile_ptr, valbuf, sizeof(valbuf));
        ui_row_two_col(y_profile, "PROFIL", valbuf);
    }

    const char* conn_status = "IDLE";
    uint16_t conn_color = UI_VAL_IDLE;
    if (strcmp((char*)display_output_mode, "usb") == 0) {
        conn_status = "USB";
        conn_color = UI_VAL_USB;
    } else if (strcmp((char*)display_output_mode, "bluetooth") == 0) {
        conn_status = "BLUETOOTH";
        conn_color = UI_VAL_BLE;
    }
    if (strcmp((char*)display_output_mode, prev_output_mode) != 0) {
        strncpy((char*)prev_output_mode, (char*)display_output_mode, 15);
        prev_output_mode[15] = '\0';
        ui_row_two_col_color(y_lia, "LIAISON", conn_status, conn_color);
    }

    const char* device_ptr = (char*)display_connected_device;
    if (!device_ptr || !device_ptr[0]) {
        device_ptr = (strcmp((char*)display_output_mode, "bluetooth") == 0) ? "SANS FIL" : "FILAIRE";
    }
    if (force_key_device || strcmp(device_ptr, prev_connected_device) != 0) {
        strncpy((char*)prev_connected_device, device_ptr, 31);
        prev_connected_device[31] = '\0';
        to_upper_str(device_ptr, valbuf, sizeof(valbuf));
        ui_row_two_col(y_host, "HOTE", valbuf);
    }

    const char* last_key_ptr = (char*)display_last_key;
    const char* last_key_display = (!last_key_ptr || !last_key_ptr[0]) ? "-" : last_key_ptr;
    if (force_key_device || strcmp(last_key_display, prev_last_key) != 0) {
        strncpy((char*)prev_last_key, last_key_display, 15);
        prev_last_key[15] = '\0';
        to_upper_str(last_key_display, valbuf, sizeof(valbuf));
        ui_row_two_col(y_key, "TOUCHE", valbuf);
    }

    uint8_t kc = display_keys_count;
    if (kc != prev_keys_count) {
        prev_keys_count = kc;
        ui_row_keys(y_map, kc);
    }

    uint16_t lv = light_level;
    if (lv != prev_light_level) {
        prev_light_level = lv;
        ui_row_ambient(y_amb, lv);
    }

    uint8_t be = display_backlight_enabled ? 1 : 0;
    uint8_t pwm = display_backlight_brightness;
    if (be != prev_backlight_enabled || pwm != prev_backlight_pwm) {
        prev_backlight_enabled = be;
        prev_backlight_pwm = pwm;
        ui_row_led(y_led, be, pwm);
    }

    const char* cz1 = (char*)display_custom1;
    const char* cz2 = (char*)display_custom2;
    uint16_t y_c1 = (uint16_t)(y_led + UI_ROW_GAP);
    uint16_t y_c2 = (uint16_t)(y_c1 + UI_ROW_GAP);
    if (strcmp(cz1, prev_c1) != 0 || strcmp(cz2, prev_c2) != 0) {
        strncpy(prev_c1, cz1, 31);
        prev_c1[31] = '\0';
        strncpy(prev_c2, cz2, 31);
        prev_c2[31] = '\0';
        if (cz1[0]) {
            to_upper_str(cz1, valbuf, sizeof(valbuf));
            ui_row_two_col_color(y_c1, "L.1", valbuf, UI_TEXT_HI);
        } else {
            ui_clear_band(y_c1, UI_ROW_GAP);
        }
        if (cz2[0]) {
            to_upper_str(cz2, valbuf, sizeof(valbuf));
            ui_row_two_col_color(y_c2, "L.2", valbuf, UI_TEXT_HI);
        } else {
            ui_clear_band(y_c2, UI_ROW_GAP);
        }
    }
}

// Alias pour compatibilité - appelle la mise à jour partielle
void display_simple_info(void) {
    if (image_receiving) {
        return;
    }
    if (strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) {
        display_draw_image_footer();
        return;
    }
    display_update_partial(0);
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
                if (tft_panel_asleep) {
                    set_led_brightness(0);
                } else {
                    set_led_brightness(brightness);
                    st7789_update_display();
                }
            }
            break;
            
        case CMD_UPDATE_DISPLAY:
            if (!tft_panel_asleep) {
                st7789_update_display();
            }
            break;
            
        case CMD_SET_DISPLAY_DATA: {
            // Parser les données d'affichage
            static char last_disp_mode[16] = "data";
            if (uart_buffer_index > 1) {
                uint8_t pos = 1;
                if (pos < uart_buffer_index) {
                    display_brightness = uart_buffer[pos++];
                }
                // Mode
                if (pos < uart_buffer_index) {
                    uint8_t mode_len = uart_buffer[pos++];
                    if (mode_len < 16 && pos + mode_len <= uart_buffer_index) {
                        memcpy((void*)display_mode, (const void*)&uart_buffer[pos], mode_len);
                        display_mode[mode_len] = '\0';
                        pos += mode_len;
                    }
                }
                // Profile
                if (pos < uart_buffer_index) {
                    uint8_t profile_len = uart_buffer[pos++];
                    if (profile_len < 32 && pos + profile_len <= uart_buffer_index) {
                        memcpy((void*)display_profile, (const void*)&uart_buffer[pos], profile_len);
                        display_profile[profile_len] = '\0';
                        pos += profile_len;
                    }
                }
                // Output mode
                if (pos < uart_buffer_index) {
                    uint8_t output_len = uart_buffer[pos++];
                    if (output_len < 16 && pos + output_len <= uart_buffer_index) {
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
                    if (last_key_len < 16) {
                        if (last_key_len > 0 && pos + last_key_len <= uart_buffer_index) {
                            memcpy((void*)display_last_key, (const void*)&uart_buffer[pos], last_key_len);
                            display_last_key[last_key_len] = '\0';
                            pos += last_key_len;
                        } else if (last_key_len == 0) {
                            display_last_key[0] = '\0';
                        }
                    }
                }
                // Backlight enabled
                if (pos < uart_buffer_index) {
                    display_backlight_enabled = uart_buffer[pos++];
                }
                // Backlight brightness
                if (pos < uart_buffer_index) {
                    display_backlight_brightness = uart_buffer[pos++];
                }
                // Time (optionnel - skip)
                if (pos < uart_buffer_index) {
                    uint8_t time_len = uart_buffer[pos++];
                    if (time_len > 0 && pos + time_len <= uart_buffer_index) {
                        pos += time_len;
                    }
                }
                // Appareil connecté (optionnel)
                if (pos < uart_buffer_index) {
                    uint8_t device_len = uart_buffer[pos++];
                    if (device_len > 0 && device_len < 32 && pos + device_len <= uart_buffer_index) {
                        memcpy((void*)display_connected_device, (const void*)&uart_buffer[pos], device_len);
                        display_connected_device[device_len] = '\0';
                        pos += device_len;
                    } else if (device_len == 0) {
                        display_connected_device[0] = '\0';
                    }
                }

                if (pos < uart_buffer_index) {
                    uint8_t c1len = uart_buffer[pos];
                    if (c1len <= 21 && pos + 1u + c1len <= uart_buffer_index) {
                        pos++;
                        for (uint8_t i = 0; i < c1len; i++) {
                            display_custom1[i] = uart_buffer[pos + i];
                        }
                        display_custom1[c1len] = '\0';
                        pos = (uint8_t)(pos + c1len);
                    }
                }
                if (pos < uart_buffer_index) {
                    uint8_t c2len = uart_buffer[pos];
                    if (c2len <= 21 && pos + 1u + c2len <= uart_buffer_index) {
                        pos++;
                        for (uint8_t i = 0; i < c2len; i++) {
                            display_custom2[i] = uart_buffer[pos + i];
                        }
                        display_custom2[c2len] = '\0';
                        pos = (uint8_t)(pos + c2len);
                    }
                }

                const uint8_t was_bitmap = (strcmp(last_disp_mode, "image") == 0 || strcmp(last_disp_mode, "gif") == 0) ? 1u : 0u;
                if (strcmp((char*)display_mode, "data") == 0 && was_bitmap) {
                    display_force_ui_reset = 1;
                }
                strncpy(last_disp_mode, (char*)display_mode, 15);
                last_disp_mode[15] = '\0';

                /* Toute écriture SPI sort le ST7789 de SLPIN — ne pas rafraîchir tant que veille volontaire. */
                if (!tft_panel_asleep) {
                    if (strcmp((char*)display_mode, "data") == 0) {
                        if (!image_receiving) {
                            display_update_partial(1);
                        }
                    } else if ((strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) && !image_receiving) {
                        display_draw_image_footer();
                    }
                }
                apply_display_backlight_from_esp32_consider_sleep();
            }
            break;
        }
            
        case CMD_SET_LAST_KEY:
            if (uart_buffer_index >= 2) {
                uint8_t last_key_len = uart_buffer[1];
                uint8_t pos = 2;
                if (last_key_len < 16) {
                    if (last_key_len > 0 && (pos + last_key_len) <= uart_buffer_index) {
                        memcpy((void*)display_last_key, (const void*)&uart_buffer[pos], last_key_len);
                        display_last_key[last_key_len] = '\0';
                        pos += last_key_len;
                    } else if (last_key_len == 0) {
                        display_last_key[0] = '\0';
                    }
                    if (pos + 2 <= uart_buffer_index) {
                        display_backlight_enabled = uart_buffer[pos++];
                        display_backlight_brightness = uart_buffer[pos++];
                    }
                    if (!tft_panel_asleep) {
                        if (!image_receiving && strcmp((char*)display_mode, "data") == 0) {
                            display_update_partial(1);
                        } else if (!image_receiving && (strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0)) {
                            display_draw_image_footer();
                        }
                    }
                    apply_display_backlight_from_esp32_consider_sleep();
                }
            }
            break;
            
        case CMD_PREPARE_SLEEP:
            if (display_initialized) {
                st7789_write_cmd(ST7789_SLPIN);
                _delay_ms(20);
            } else {
                backlight_shutdown_for_panel_sleep();
            }
            break;

        case CMD_RESUME_FROM_SLEEP: {
            /* Si SET_DISPLAY_DATA est passé pendant SLPIN, le dessin a été sauté : réveil seul ne repeint pas. */
            uint8_t did_wake_panel = 0;
            // Panneau en SLPIN : sortie explicite (ne pas passer par st7789_write_cmd(SLPIN)).
            if (tft_panel_asleep && display_initialized) {
                did_wake_panel = 1;
                ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
                ST7789_DC_PORT &= ~(1 << ST7789_DC_PIN);
                spi_write(ST7789_SLPOUT);
                ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
                _delay_ms(120);
                tft_panel_asleep = 0;
                st7789_write_cmd(ST7789_DISPON);
                _delay_ms(20);
            }
            if (uart_buffer_index >= 3) {
                display_backlight_enabled = uart_buffer[1] ? 1 : 0;
                display_backlight_brightness = uart_buffer[2];
                esp32_backlight_ticks = 200;
                set_led_brightness(display_backlight_enabled ? display_backlight_brightness : 0);
            }
            if (display_initialized && !tft_panel_asleep && did_wake_panel) {
                if (strcmp((char*)display_mode, "data") == 0) {
                    if (!image_receiving) {
                        display_force_ui_reset = 1;
                        display_update_partial(1);
                    }
                } else if ((strcmp((char*)display_mode, "image") == 0 || strcmp((char*)display_mode, "gif") == 0) && !image_receiving) {
                    display_draw_image_footer();
                }
                apply_display_backlight_from_esp32_consider_sleep();
            }
            break;
        }

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
            if (tft_panel_asleep) {
                break;
            }
            image_expected_size = (uint32_t)ST7789_WIDTH * (uint32_t)ST7789_HEIGHT * 2u;
            image_received_bytes = 0;
            image_chunk_index = 0;
            image_receiving = 1;
            LOG_INFO("[UART] Image RX start bytes=");
            debug_print_u32(image_expected_size);
            debug_print("\r\n");
            break;

        case CMD_SET_DISPLAY_IMAGE_CHUNK:
            if (tft_panel_asleep) {
                image_receiving = 0;
                break;
            }
            if (image_receiving && uart_buffer_index >= 4) {
                uint8_t chunk_size = uart_buffer[3];

                if (chunk_size >= 2 && chunk_size <= IMAGE_CHUNK_SIZE &&
                    (chunk_size % 2u) == 0 &&
                    (uart_buffer_index - 4) >= chunk_size) {

                    uint32_t byte_off = image_received_bytes;
                    uint32_t ps = byte_off / 2u;
                    uint16_t x = (uint16_t)(ps % ST7789_WIDTH);
                    uint16_t y = (uint16_t)(ps / ST7789_WIDTH);
                    uint16_t px = (uint16_t)(chunk_size / 2u);
                    if ((uint32_t)x + px <= ST7789_WIDTH && byte_off + chunk_size <= image_expected_size) {
                        st7789_set_window(x, y, (uint16_t)(x + px - 1), y);
                        ST7789_CS_PORT &= ~(1 << ST7789_CS_PIN);
                        ST7789_DC_PORT |= (1 << ST7789_DC_PIN);
                        for (uint16_t i = 0; i < chunk_size; i++) {
                            spi_write(uart_buffer[4 + i]);
                        }
                        ST7789_CS_PORT |= (1 << ST7789_CS_PIN);
                        image_received_bytes += chunk_size;
                        image_chunk_index++;

                        if (image_received_bytes >= image_expected_size) {
                            image_receiving = 0;
                            LOG_INFO("[UART] Image complete ");
                            debug_print_u32(image_received_bytes);
                            debug_print("\r\n");
                            display_draw_image_footer();
                        }
                    }
                }
            }
            break;
    }
    
    // Réinitialiser le buffer UART courant (ligne déjà copiée depuis la file)
    uart_buffer_index = 0;
    uart_command = 0;
}

// Interruption UART (réception) — une ligne complète → file pour processUartCommand
ISR(USART_RX_vect) {
    uint8_t received = UDR0;

    if (received == '\n' || received == '\r') {
        if (uart_rx_line_len > 0) {
            if (uart_line_q_count < UART_LINE_QUEUE_DEPTH) {
                uint8_t i = uart_line_q_in;
                uint8_t n = uart_rx_line_len;
                if (n >= UART_BUFFER_SIZE) {
                    n = (uint8_t)(UART_BUFFER_SIZE - 1);
                }
                memcpy((void*)uart_line_queue[i], uart_rx_line, n);
                uart_line_len[i] = n;
                uart_line_q_in = (uint8_t)((uart_line_q_in + 1u) % UART_LINE_QUEUE_DEPTH);
                uart_line_q_count++;
            }
            uart_rx_line_len = 0;
        }
    } else {
        if (uart_rx_line_len < UART_BUFFER_SIZE - 1) {
            uart_rx_line[uart_rx_line_len++] = received;
        } else {
            uart_rx_line_len = 0;
        }
    }
}