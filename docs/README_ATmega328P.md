# Configuration ATmega328P/168A pour Macropad

Guide de configuration et de programmation de l'ATmega328P ou ATmega168A pour la détection de touches dans la matrice du macropad.

## 📦 Matériel

- **Microcontrôleur** : ATmega328P ou ATmega168A
- **Programmeur** : PICKit 4 (fourni par l'école)
- **IDE** : Microchip Studio (anciennement Atmel Studio)
- **Fonction** : Détection de touches dans la matrice 4×5 et communication I2C avec ESP32-S3

## 🔧 Configuration Microchip Studio

### Installation

1. Téléchargez **Microchip Studio** depuis le site officiel de Microchip
2. Installez le logiciel avec les composants par défaut
3. Assurez-vous que les outils pour AVR sont inclus

### Configuration du projet

1. Ouvrez Microchip Studio
2. **File > New > Project**
3. Sélectionnez **GCC C Executable Project**
4. Choisissez **ATmega328P** ou **ATmega168A** comme device
5. Configurez le projet

### Configuration du programmeur

1. Connectez le PICKit 4 à l'ATmega
2. **Tools > External Tools > PICKit 4**
3. Configurez les connexions :
   - **VDD** : 5V (ou 3.3V selon votre configuration)
   - **GND** : Masse
   - **PGC** : Pin de programmation clock
   - **PGD** : Pin de programmation data
   - **MCLR** : Reset

## 📚 Librairies et fichiers d'en-tête

### Fichiers nécessaires

Créez ou incluez les fichiers suivants dans votre projet :

1. **i2c_slave.h / i2c_slave.c** : Communication I2C en mode esclave
2. **matrix_scan.h / matrix_scan.c** : Scan de la matrice de touches
3. **config.h** : Configuration et définitions

### Exemple de structure

```
atmega_macropad/
├── main.c                    # Fichier principal
├── i2c_slave.h              # En-tête I2C esclave
├── i2c_slave.c              # Implémentation I2C esclave
├── matrix_scan.h            # En-tête scan matrice
├── matrix_scan.c            # Implémentation scan matrice
├── config.h                 # Configuration
└── Makefile                 # Makefile pour compilation
```

## 🔌 Brochage (Pins)

### Matrice de touches 4×5

**Lignes (Rows)** - Sorties, mises à HIGH avec pull-up :
- Row 0 : PB0 (Pin 14)
- Row 1 : PB1 (Pin 15)
- Row 2 : PB2 (Pin 16)
- Row 3 : PB3 (Pin 17)
- Row 4 : PB4 (Pin 18)

**Colonnes (Cols)** - Entrées, lues avec pull-down :
- Col 0 : PC0 (Pin 23)
- Col 1 : PC1 (Pin 24)
- Col 2 : PC2 (Pin 25)
- Col 3 : PC3 (Pin 26)

### Communication I2C

- **SDA** : PC4 (Pin 27) - SDA
- **SCL** : PC5 (Pin 28) - SCL
- **Adresse I2C** : 0x08 (configurable)
- **Résistances de pull-up** : 4.7kΩ à 3.3V sur chaque ligne (SDA et SCL)
- **Note** : PC4 et PC5 sont les pins I2C standard de l'ATmega328P/168A, gérés automatiquement par le module TWI

### Alimentation

- **VCC** : 5V ou 3.3V (selon configuration)
- **GND** : Masse
- **AVCC** : Connecté à VCC
- **AREF** : Peut être laissé flottant ou connecté à VCC

## 💻 Code exemple

### main.c

```c
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "i2c_slave.h"
#include "matrix_scan.h"
#include "config.h"

// État des touches
volatile uint16_t key_state = 0;
volatile uint8_t key_changed = 0;

int main(void) {
    // Initialiser la matrice
    matrix_init();
    
    // Initialiser I2C en mode esclave
    i2c_slave_init(0x08); // Adresse I2C 0x08
    
    // Activer les interruptions globales
    sei();
    
    while (1) {
        // Scanner la matrice
        uint16_t new_state = matrix_scan();
        
        // Détecter les changements
        if (new_state != key_state) {
            key_state = new_state;
            key_changed = 1;
        }
        
        // Petit délai pour éviter le rebond
        _delay_ms(10);
    }
    
    return 0;
}
```

### i2c_slave.h

```c
#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdint.h>

// Initialiser I2C en mode esclave
void i2c_slave_init(uint8_t address);

// Obtenir l'état des touches
uint16_t i2c_get_key_state(void);

// Vérifier si les touches ont changé
uint8_t i2c_key_changed(void);

#endif
```

### i2c_slave.c

```c
#include "i2c_slave.h"
#include <avr/io.h>
#include <avr/interrupt.h>

static volatile uint16_t i2c_key_state = 0;
static volatile uint8_t i2c_key_changed_flag = 0;

void i2c_slave_init(uint8_t address) {
    // Configurer l'adresse I2C
    TWAR = (address << 1) | 1; // Générer l'adresse avec bit R/W
    
    // Activer I2C et interruptions
    TWCR = (1 << TWIE) | (1 << TWEA) | (1 << TWEN) | (1 << TWINT);
}

uint16_t i2c_get_key_state(void) {
    return i2c_key_state;
}

uint8_t i2c_key_changed(void) {
    if (i2c_key_changed_flag) {
        i2c_key_changed_flag = 0;
        return 1;
    }
    return 0;
}

// Interruption I2C
ISR(TWI_vect) {
    uint8_t status = TWSR & 0xF8;
    
    switch (status) {
        case 0x60: // SLA+W reçu, ACK retourné
        case 0x68: // SLA+W reçu après arbitration, ACK retourné
        case 0x70: // Général call reçu, ACK retourné
        case 0x78: // Général call reçu après arbitration, ACK retourné
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0x80: // Données reçues, ACK retourné
            // Lire les données (si nécessaire)
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0x88: // Données reçues, NACK retourné
            TWCR |= (1 << TWINT);
            break;
            
        case 0xA0: // STOP ou répété START reçu
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0xA8: // SLA+R reçu, ACK retourné
            // Envoyer l'état des touches
            TWDR = (uint8_t)(i2c_key_state & 0xFF);
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0xB0: // SLA+R reçu après arbitration, ACK retourné
            TWDR = (uint8_t)(i2c_key_state & 0xFF);
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0xB8: // Données transmises, ACK reçu
            // Envoyer le deuxième byte
            TWDR = (uint8_t)((i2c_key_state >> 8) & 0xFF);
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0xC0: // Données transmises, NACK reçu
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        case 0xC8: // Dernière donnée transmise, ACK reçu
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
            
        default:
            TWCR |= (1 << TWEA) | (1 << TWINT);
            break;
    }
}
```

### matrix_scan.h

```c
#ifndef MATRIX_SCAN_H
#define MATRIX_SCAN_H

#include <stdint.h>

// Initialiser la matrice
void matrix_init(void);

// Scanner la matrice et retourner l'état des touches
uint16_t matrix_scan(void);

#endif
```

### matrix_scan.c

```c
#include "matrix_scan.h"
#include <avr/io.h>
#include <util/delay.h>

#define ROW_COUNT 5
#define COL_COUNT 4

// Pins des lignes (sorties)
#define ROW_PORT PORTB
#define ROW_DDR  DDRB
#define ROW_PIN  PINB

// Pins des colonnes (entrées)
#define COL_PORT PORTC
#define COL_DDR  DDRC
#define COL_PIN  PINC

void matrix_init(void) {
    // Configurer les lignes comme sorties (HIGH par défaut avec pull-up)
    ROW_DDR |= 0x1F; // PB0-PB4
    ROW_PORT |= 0x1F;
    
    // Configurer les colonnes comme entrées (pull-down via résistances externes)
    COL_DDR &= ~0x0F; // PC0-PC3
    COL_PORT &= ~0x0F; // Pas de pull-up interne
}

uint16_t matrix_scan(void) {
    uint16_t state = 0;
    uint8_t bit = 0;
    
    // Scanner chaque ligne
    for (uint8_t row = 0; row < ROW_COUNT; row++) {
        // Mettre la ligne courante à LOW
        ROW_PORT &= ~(1 << row);
        _delay_us(10); // Petit délai pour stabilisation
        
        // Lire les colonnes
        uint8_t cols = COL_PIN & 0x0F;
        
        // Vérifier chaque colonne
        for (uint8_t col = 0; col < COL_COUNT; col++) {
            if (!(cols & (1 << col))) {
                // Touche pressée (LOW car pull-down)
                state |= (1 << bit);
            }
            bit++;
        }
        
        // Remettre la ligne à HIGH
        ROW_PORT |= (1 << row);
        _delay_us(10);
    }
    
    return state;
}
```

### config.h

```c
#ifndef CONFIG_H
#define CONFIG_H

// Fréquence du CPU
#define F_CPU 16000000UL

// Configuration I2C
#define I2C_ADDRESS 0x08
#define I2C_FREQ 100000 // 100kHz

// Configuration matrice
#define MATRIX_ROWS 5
#define MATRIX_COLS 4
#define MATRIX_TOTAL_KEYS (MATRIX_ROWS * MATRIX_COLS)

#endif
```

## 🔄 Communication I2C avec ESP32-S3

### Protocole

L'ATmega agit comme **esclave I2C** avec l'adresse **0x08**.

### Format des données

Quand l'ESP32 demande l'état des touches :

1. **Byte 0** : État des touches (bits 0-7)
2. **Byte 1** : État des touches (bits 8-15)

Chaque bit représente une touche :
- Bit 0 : Touche 0,0 (row 0, col 0)
- Bit 1 : Touche 0,1 (row 0, col 1)
- ...
- Bit 19 : Touche 4,3 (row 4, col 3)

### Mapping des touches

```
Row\Col | 0    | 1    | 2    | 3
--------|------|------|------|------
0       | 0-0  | 0-1  | 0-2  | 0-3
1       | 1-0  | 1-1  | 1-2  | 1-3
2       | 2-0  | 2-1  | 2-2  | 2-3
3       | 3-0  | 3-1  | 3-2  | 3-3
4       | 4-0  | 4-1  | 4-2  | 4-3
```

## 🔧 Configuration du compilateur

### Fuses (Fusibles)

Configuration recommandée pour ATmega328P :

- **Low Fuse** : 0xFF (Clock externe, pas de division)
- **High Fuse** : 0xDE (Boot reset, SPI enabled)
- **Extended Fuse** : 0xFD (Brown-out à 2.7V)

### Fréquence d'horloge

- **16 MHz** : Utilisez un cristal externe 16MHz
- **8 MHz** : Si vous utilisez l'oscillateur interne

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
4. Sélectionnez **ATmega328P** ou **ATmega168A** comme device
5. Cliquez sur **Apply**
6. Vérifiez les fuses si nécessaire
7. **Memories > Flash** : Sélectionnez le fichier .hex
8. Cliquez sur **Program**

## 🐛 Dépannage

### L'ATmega ne répond pas sur I2C

- Vérifiez les connexions SDA/SCL
- Vérifiez les résistances de pull-up (4.7kΩ sur SDA et SCL)
- Vérifiez l'adresse I2C avec un scanner
- Vérifiez que l'ATmega est alimenté correctement

### La matrice ne détecte pas les touches

- Vérifiez les connexions des lignes et colonnes
- Vérifiez que les diodes sont orientées correctement (si utilisées)
- Vérifiez les résistances de pull-down sur les colonnes
- Testez chaque ligne/colonne individuellement

### Le PICKit 4 ne détecte pas l'ATmega

- Vérifiez toutes les connexions
- Vérifiez que l'ATmega est alimenté
- Essayez de réinitialiser le PICKit 4
- Vérifiez que le bon device est sélectionné

## 📚 Ressources

- [Documentation ATmega328P](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [Microchip Studio](https://www.microchip.com/en-us/tools-resources/develop/microchip-studio)
- [PICKit 4 User Guide](https://ww1.microchip.com/downloads/en/DeviceDoc/50002729B.pdf)

## 📝 Notes

- L'ATmega328P a 32KB de flash, l'ATmega168A a 16KB
- Utilisez des résistances de pull-up (4.7kΩ) sur SDA et SCL pour I2C
- Utilisez des résistances de pull-down sur les colonnes de la matrice
- Le scan de matrice doit être fait régulièrement (toutes les 10-20ms)
- Utilisez un délai anti-rebond pour éviter les faux positifs
