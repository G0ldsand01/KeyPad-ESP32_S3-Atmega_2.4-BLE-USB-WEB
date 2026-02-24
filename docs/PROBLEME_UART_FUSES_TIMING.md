# Problème UART : Fuses et Timing

## Analyse des problèmes

### 1. **Fuses ATmega328P**

D'après l'image MPLAB, les fuses actuelles sont :
- **EXTENDED**: `0xFD` ✅ (Correct)
- **HIGH**: `0xD6` ⚠️ (À vérifier)
- **LOW**: `0xE2` ❌ **PROBLÈME POTENTIEL**

#### Analyse du LOW Fuse (0xE2)

Le LOW fuse `0xE2` en binaire = `1110 0010` signifie :
- **CKSEL[3:0]** = `0010` → Oscillateur calibré RC interne (8MHz)
- **SUT[1:0]** = `10` → Start-up time: 6 CK + 64ms
- **CKOUT** = `0` → Clock output disabled
- **CKDIV8** = `1` → **Division par 8 activée** ❌

**PROBLÈME** : Si `CKDIV8` est activé, le CPU tourne à 1MHz au lieu de 16MHz, ce qui rend le baudrate UART incorrect !

#### Fuses recommandées pour ATmega328P @ 16MHz externe

Pour un oscillateur externe à 16MHz :
- **LOW**: `0xFF` ou `0xF7` (oscillateur externe, pas de division)
- **HIGH**: `0xDE` (bootloader, SPIEN activé)
- **EXTENDED**: `0xFD` (BODLEVEL) ✅

### 2. **Problème de Timing**

L'ESP32 envoie des commandes très tôt après le démarrage :

```python
time.sleep(1)  # Attendre seulement 1 seconde
```

Mais l'ATmega prend du temps à :
1. Initialiser l'UART (~100ms)
2. Initialiser l'ADC (~50ms)
3. Initialiser le PWM (~50ms)
4. Initialiser le SPI (~50ms)
5. Initialiser l'écran ST7789 (~200-500ms)
6. Afficher l'écran de démarrage (~100ms)

**Total** : ~550-850ms minimum, mais peut être plus long selon l'écran.

### 3. **Baudrate UART**

Si l'oscillateur n'est pas à 16MHz exactement (à cause de `CKDIV8` ou autre), le baudrate sera incorrect.

Pour 115200 bauds avec F_CPU = 16MHz :
```
UBRR = (16000000 / 16 / 115200) - 1 = 7.68 ≈ 8
```

Mais si F_CPU = 1MHz (avec CKDIV8) :
```
UBRR = (1000000 / 16 / 115200) - 1 = -0.46 ❌ IMPOSSIBLE
```

## Solutions

### Solution 1 : Corriger les Fuses

1. **Ouvrir MPLAB IPE**
2. **Aller dans "Fuses"**
3. **Modifier le LOW fuse** :
   - **CKSEL** : Sélectionner "External Clock" ou "External Crystal" selon votre configuration
   - **CKDIV8** : **DÉSACTIVER** (décocher) ❗
   - **SUT** : Laisser par défaut
4. **Cliquer sur "Program"** pour flasher les nouvelles fuses

**Valeurs recommandées** :
- **LOW**: `0xFF` (oscillateur externe, pas de division)
- **HIGH**: `0xD6` (peut rester tel quel si SPIEN est activé)
- **EXTENDED**: `0xFD` (peut rester tel quel)

### Solution 2 : Augmenter le délai de démarrage ESP32

Modifier `firmware/esp32_micropython/main.py` :

```python
# Dans la fonction main(), après init_atmega_uart()
print("[MAIN] Waiting for ATmega to boot...")
time.sleep(2)  # Augmenter de 1 à 2 secondes
```

### Solution 3 : Vérifier l'oscillateur

1. **Mesurer la fréquence** sur le pin XTAL1 ou XTAL2 avec un oscilloscope
2. **Vérifier que c'est bien 16MHz** (ou la fréquence configurée)
3. **Si ce n'est pas 16MHz**, ajuster `F_CPU` dans le code ATmega

### Solution 4 : Test de communication bidirectionnelle

Ajouter un test de ping-pong pour vérifier que la communication fonctionne :

**ESP32** :
```python
# Envoyer un ping
send_atmega_command(0x01, b"")  # CMD_READ_LIGHT comme ping
time.sleep(0.1)
# Lire la réponse
data = atmega_uart.read()
if data:
    print("[UART] ATmega responded!")
else:
    print("[UART] No response from ATmega")
```

## Diagnostic étape par étape

### Étape 1 : Vérifier les fuses
- [ ] Ouvrir MPLAB IPE
- [ ] Lire les fuses actuelles
- [ ] Vérifier que `CKDIV8` est désactivé
- [ ] Vérifier que `CKSEL` correspond à votre oscillateur

### Étape 2 : Vérifier le timing
- [ ] Augmenter le délai de démarrage à 2-3 secondes
- [ ] Vérifier que l'ATmega a le temps de démarrer

### Étape 3 : Vérifier le baudrate
- [ ] Mesurer la fréquence de l'oscillateur
- [ ] Vérifier que `F_CPU` dans le code correspond à la fréquence réelle
- [ ] Recalculer `UBRR` si nécessaire

### Étape 4 : Test de communication
- [ ] Envoyer une commande simple (CMD_READ_LIGHT)
- [ ] Vérifier qu'une réponse est reçue
- [ ] Analyser les données reçues (hex dump)

## Notes importantes

1. **Ne jamais modifier les fuses sans comprendre** ce que vous faites
2. **Toujours sauvegarder** les fuses actuelles avant modification
3. **Vérifier la documentation** de votre oscillateur (interne vs externe)
4. **Tester après chaque modification** de fuse

## Références

- [ATmega328P Datasheet - Fuse Bits](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [UART Baudrate Calculator](https://www.avrfreaks.net/forum/baud-rate-calculator)
