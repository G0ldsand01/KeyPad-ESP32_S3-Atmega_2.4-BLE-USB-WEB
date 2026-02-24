# Configuration Complète des Fuses - ATmega328P

## Configuration Actuelle (Oscillateur RC Interne 8 MHz)

D'après votre configuration actuelle dans MPLAB IPE :

### Fuses Actuelles
- **EXTENDED**: `0xFD`
- **HIGH**: `0xD6`
- **LOW**: `0xE2`

### Détail des Fuses Actuelles

#### EXTENDED Fuse (0xFD = 1111 1101)
- **BODLEVEL[2:0]** = `101` → Brown-out detection at VCC=2.7V ✅
- **Reserved** = `11111` → Réservé (doit rester à 1)

#### HIGH Fuse (0xD6 = 1101 0110)
- **RSTDISBL** = `0` → Reset pin enabled (normal) ✅
- **DWEN** = `0` → DebugWIRE disabled ✅
- **SPIEN** = `1` → SPI programming enabled ✅
- **WDTON** = `0` → Watchdog timer always on disabled ✅
- **EESAVE** = `1` → EEPROM preserved during chip erase ✅
- **BOOTSZ[1:0]** = `01` → Boot Flash size = 256 words, start address = $3F00
- **BOOTRST** = `1` → Boot reset vector enabled (bootloader)

#### LOW Fuse (0xE2 = 1110 0010)
- **CKDIV8** = `0` → No clock division ✅
- **CKOUT** = `0` → Clock output disabled ✅
- **SUT[1:0]** = `10` → Start-up time: 6 CK + 64ms
- **CKSEL[3:0]** = `0010` → **Int. RC Osc. 8 MHz** ⚠️

## Configuration Recommandée #1 : Oscillateur RC Interne 8 MHz (Actuel)

Si vous gardez l'oscillateur RC interne à 8 MHz :

### Fuses Recommandées
- **EXTENDED**: `0xFD` (identique)
- **HIGH**: `0xD6` (identique)
- **LOW**: `0xE2` (identique) ✅

**Note** : Vos fuses actuelles sont correctes pour un oscillateur RC interne 8 MHz !

### Détail LOW Fuse (0xE2)
- **CKSEL[3:0]** = `0010` → Int. RC Osc. 8 MHz
- **SUT[1:0]** = `10` → Start-up time: 6 CK + 64ms
- **CKOUT** = `0` → Clock output disabled
- **CKDIV8** = `0` → No clock division

**Important** : Assurez-vous que `F_CPU = 8000000UL` dans votre code !

## Configuration Recommandée #2 : Oscillateur Externe 16 MHz (Meilleure Précision)

Pour une meilleure précision UART et performance :

### Fuses Recommandées
- **EXTENDED**: `0xFD` (identique)
- **HIGH**: `0xD6` (identique) ou `0xDE` (sans bootloader)
- **LOW**: `0xFF` ou `0xF7` (oscillateur externe)

### Détail LOW Fuse pour Oscillateur Externe

#### Option A : Crystal Oscillator 8.0-16.0 MHz (0xFF)
- **CKSEL[3:0]** = `1111` → Ext. Crystal Osc. 8.0-16.0 MHz
- **SUT[1:0]** = `11` → Start-up time: 16K CK + 4.1ms
- **CKOUT** = `0` → Clock output disabled
- **CKDIV8** = `0` → No clock division

#### Option B : External Clock (0xF7)
- **CKSEL[3:0]** = `1111` → External Clock
- **SUT[1:0]** = `11` → Start-up time: 6 CK + 4ms
- **CKOUT** = `0` → Clock output disabled
- **CKDIV8** = `0` → No clock division

**Important** : Si vous utilisez un oscillateur externe 16 MHz, changez `F_CPU = 16000000UL` dans votre code !

## Tableau Récapitulatif des Fuses

| Fuse | Valeur Actuelle | Valeur Recommandée (8MHz RC) | Valeur Recommandée (16MHz Ext) |
|------|----------------|------------------------------|--------------------------------|
| **EXTENDED** | `0xFD` | `0xFD` | `0xFD` |
| **HIGH** | `0xD6` | `0xD6` | `0xD6` ou `0xDE` |
| **LOW** | `0xE2` | `0xE2` | `0xFF` ou `0xF7` |

## Explication Détaillée de Chaque Fuse

### EXTENDED Fuse (0xFD)

| Bit | Nom | Valeur | Description |
|-----|-----|--------|-------------|
| 7 | - | 1 | Reserved (doit être 1) |
| 6 | - | 1 | Reserved (doit être 1) |
| 5 | - | 1 | Reserved (doit être 1) |
| 4 | - | 1 | Reserved (doit être 1) |
| 3 | - | 1 | Reserved (doit être 1) |
| 2 | BODLEVEL2 | 1 | Brown-out detection level bit 2 |
| 1 | BODLEVEL1 | 0 | Brown-out detection level bit 1 |
| 0 | BODLEVEL0 | 1 | Brown-out detection level bit 0 |

**BODLEVEL = 101** → Brown-out detection at VCC=2.7V
- Protège contre les baisses de tension
- Recommandé pour la stabilité

### HIGH Fuse (0xD6)

| Bit | Nom | Valeur | Description |
|-----|-----|--------|-------------|
| 7 | RSTDISBL | 0 | Reset pin enabled (normal) ✅ |
| 6 | DWEN | 0 | DebugWIRE disabled ✅ |
| 5 | SPIEN | 1 | SPI programming enabled ✅ |
| 4 | WDTON | 0 | Watchdog timer always on disabled ✅ |
| 3 | EESAVE | 1 | EEPROM preserved during chip erase ✅ |
| 2 | BOOTSZ1 | 0 | Boot Flash size bit 1 |
| 1 | BOOTSZ0 | 1 | Boot Flash size bit 0 |
| 0 | BOOTRST | 1 | Boot reset vector enabled |

**BOOTSZ = 01** → Boot Flash size = 256 words, start address = $3F00
- Si vous n'utilisez pas de bootloader, vous pouvez mettre BOOTRST = 0
- HIGH = `0xDE` sans bootloader (BOOTRST = 0)

### LOW Fuse (0xE2 pour RC 8MHz, 0xFF pour Ext 16MHz)

#### Pour Oscillateur RC Interne 8 MHz (0xE2)

| Bit | Nom | Valeur | Description |
|-----|-----|--------|-------------|
| 7 | CKDIV8 | 0 | No clock division ✅ |
| 6 | CKOUT | 0 | Clock output disabled ✅ |
| 5 | SUT1 | 1 | Start-up time bit 1 |
| 4 | SUT0 | 0 | Start-up time bit 0 |
| 3 | CKSEL3 | 0 | Clock select bit 3 |
| 2 | CKSEL2 | 0 | Clock select bit 2 |
| 1 | CKSEL1 | 1 | Clock select bit 1 |
| 0 | CKSEL0 | 0 | Clock select bit 0 |

**CKSEL = 0010** → Int. RC Osc. 8 MHz
**SUT = 10** → Start-up time: 6 CK + 64ms

#### Pour Oscillateur Externe 16 MHz (0xFF)

| Bit | Nom | Valeur | Description |
|-----|-----|--------|-------------|
| 7 | CKDIV8 | 0 | No clock division ✅ |
| 6 | CKOUT | 0 | Clock output disabled ✅ |
| 5 | SUT1 | 1 | Start-up time bit 1 |
| 4 | SUT0 | 1 | Start-up time bit 0 |
| 3 | CKSEL3 | 1 | Clock select bit 3 |
| 2 | CKSEL2 | 1 | Clock select bit 2 |
| 1 | CKSEL1 | 1 | Clock select bit 1 |
| 0 | CKSEL0 | 1 | Clock select bit 0 |

**CKSEL = 1111** → Ext. Crystal Osc. 8.0-16.0 MHz
**SUT = 11** → Start-up time: 16K CK + 4.1ms

## Instructions pour MPLAB IPE

### Configuration Actuelle (Garder tel quel si vous restez avec RC 8MHz)

1. **Ouvrir MPLAB IPE**
2. **Sélectionner "Fuses"**
3. **Vérifier les valeurs** :
   - EXTENDED: `0xFD`
   - HIGH: `0xD6`
   - LOW: `0xE2`
4. **Vérifier les options** :
   - ✅ LOW.CKDIV8 : **DÉCOCHÉ** (important !)
   - ✅ HIGH.SPIEN : **COCHÉ** (pour programmer)
   - ✅ LOW.SUT_CKSEL : "Int. RC Osc. 8 MHz"

### Si vous voulez passer à Oscillateur Externe 16 MHz

1. **Connecter un cristal 16 MHz** entre XTAL1 et XTAL2 avec condensateurs 22pF
2. **Dans MPLAB IPE - Fuses** :
   - LOW.SUT_CKSEL : Changer pour "Ext. Crystal Osc. 8.0-16.0 MHz"
   - LOW fuse devrait devenir `0xFF`
3. **Programmer les nouvelles fuses**
4. **Modifier le code** : `F_CPU = 16000000UL`

## Vérification

Après avoir configuré les fuses :

1. **Lire les fuses** dans MPLAB IPE pour vérifier
2. **Vérifier que CKDIV8 est désactivé** (décoché)
3. **Vérifier que SPIEN est activé** (coché) pour pouvoir reprogrammer
4. **Recompiler le firmware** avec la bonne valeur de F_CPU
5. **Flasher le firmware**

## Notes Importantes

⚠️ **ATTENTION** :
- Ne jamais désactiver **SPIEN** si vous voulez pouvoir reprogrammer le microcontrôleur !
- Ne jamais modifier les bits réservés (doivent rester à 1)
- Toujours sauvegarder les fuses actuelles avant modification
- Si vous désactivez RSTDISBL, le pin RESET devient une entrée GPIO normale

✅ **Recommandations** :
- Garder BODLEVEL activé pour la protection contre les baisses de tension
- Garder EESAVE activé si vous utilisez l'EEPROM
- Utiliser un oscillateur externe pour une meilleure précision UART

## Références

- [ATmega328P Datasheet - Fuse Bits](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [AVR Fuse Calculator](https://www.engbedded.com/fusecalc/)
