# Diagnostic Écran Noir - ST7789

## Problème

L'écran reste noir après le démarrage de l'ATmega328P.

## Causes Possibles

### 1. **Backlight non activé** ⚠️ PROBABLE

Le backlight (LED PWM sur PD5) pourrait ne pas être activé.

**Solution appliquée** : Le code active maintenant le backlight à 128/255 par défaut.

**Vérification** :
- Mesurer la tension sur PD5 (Pin 19) avec un multimètre
- Devrait être ~2.5V si le PWM est à 50% (128/255)
- Si 0V → Le PWM ne fonctionne pas

### 2. **Délais insuffisants avec F_CPU = 8MHz**

Avec F_CPU = 8MHz (au lieu de 16MHz), tous les délais sont maintenant 2x plus longs.

**Solution appliquée** : Les délais critiques ont été augmentés :
- `_delay_ms(200)` après DISPON (était 100ms)
- `_delay_ms(100)` après fill_screen (était 50ms)

### 3. **Problème de câblage SPI**

Vérifier les connexions :
- **MOSI (PB3, Pin 17)** → ST7789 SDA
- **SCK (PB5, Pin 19)** → ST7789 SCL
- **CS (PB2, Pin 16)** → ST7789 CS
- **DC (PB1, Pin 15)** → ST7789 DC
- **RST (PB0, Pin 14)** → ST7789 RST (ou VCC si pas utilisé)
- **VCC** → 3.3V ou 5V (selon votre écran)
- **GND** → GND commun

### 4. **Problème d'alimentation**

L'écran ST7789 nécessite une alimentation stable.

**Vérification** :
- Mesurer VCC de l'écran (devrait être 3.3V ou 5V selon le modèle)
- Vérifier que GND est bien connecté
- Vérifier que le courant disponible est suffisant (l'écran consomme ~50-100mA)

### 5. **Écran non initialisé correctement**

L'écran pourrait ne pas recevoir les commandes d'initialisation.

**Diagnostic** :
- Vérifier les logs UART de l'ATmega (via Serial Monitor)
- Chercher les messages `[ST7789] Display ON command sent`
- Si ces messages n'apparaissent pas → L'initialisation échoue

## Solutions Appliquées dans le Code

### 1. Activation du Backlight

```cpp
// Dans main(), après initialisation de l'écran
display_backlight_enabled = 1;
display_backlight_brightness = 128;
set_led_brightness(128);  // Activer le backlight immédiatement
```

### 2. Délais Augmentés

```cpp
// Dans st7789_init()
st7789_write_cmd(ST7789_DISPON);
_delay_ms(200);  // Augmenté de 100ms à 200ms

st7789_fill_screen(black);
_delay_ms(100);  // Augmenté de 50ms à 100ms
```

### 3. Affichage Forcé

```cpp
// Dans main(), après initialisation
display_simple_info();  // Forcer l'affichage des informations
```

### 4. Logs de Diagnostic

Des messages de debug ont été ajoutés pour tracer l'initialisation :
- `[ST7789] Display ON command sent`
- `[ST7789] Screen filled with black`
- `[ST7789] Initial display drawn`
- `[LED] Brightness set to: XXX`

## Étapes de Diagnostic

### Étape 1 : Vérifier les Logs UART

1. Connecter un adaptateur USB-Serial à l'ATmega (TX sur PD1, Pin 3)
2. Ouvrir un Serial Monitor à 115200 bauds
3. Redémarrer l'ATmega
4. Vérifier les messages :
   - `=== ATmega328P Light Controller ===`
   - `ST7789 initialized`
   - `[ST7789] Display ON command sent`
   - `[ST7789] Screen filled with black`
   - `[ST7789] Initial display drawn`
   - `[LED] Brightness set to: 128`

**Si ces messages n'apparaissent pas** → Le problème est dans l'initialisation de base.

### Étape 2 : Vérifier le Backlight

1. **Mesurer la tension sur PD5 (Pin 19)** :
   - Devrait être ~2.5V si PWM = 128/255
   - Si 0V → Le PWM ne fonctionne pas
   - Si 5V → Le PWM est à 100%

2. **Test manuel** : Connecter temporairement PD5 à VCC via une résistance 220Ω
   - Si l'écran s'allume → Le problème vient du PWM
   - Si l'écran reste noir → Le problème vient de l'écran lui-même

### Étape 3 : Vérifier le SPI

1. **Oscilloscope** : Vérifier les signaux SPI sur MOSI et SCK
2. **Multimètre** : Vérifier que CS et DC changent d'état
3. **Test simple** : Essayer d'envoyer une commande simple et vérifier la réponse

### Étape 4 : Vérifier l'Alimentation

1. Mesurer VCC de l'écran (devrait être stable)
2. Vérifier que le courant disponible est suffisant
3. Vérifier que GND est bien connecté

## Test Rapide

Pour tester rapidement si l'écran fonctionne :

1. **Forcer le backlight à 255** (maximum) :
   ```cpp
   set_led_brightness(255);
   ```

2. **Remplir l'écran en blanc** :
   ```cpp
   st7789_fill_screen(0xFFFF);  // Blanc
   ```

3. **Remplir l'écran en rouge** :
   ```cpp
   st7789_fill_screen(0xF800);  // Rouge RGB565
   ```

Si l'écran reste noir même avec ces tests → Problème matériel (câblage ou écran défectueux).

## Prochaines Étapes

1. **Recompiler le firmware** avec les modifications (backlight activé, délais augmentés)
2. **Flasher le nouveau firmware** sur l'ATmega
3. **Vérifier les logs UART** pour voir où ça bloque
4. **Tester le backlight** avec un multimètre
5. **Vérifier le câblage SPI** si nécessaire

## Notes Importantes

- Avec F_CPU = 8MHz, tous les délais sont maintenant 2x plus longs
- Le SPI fonctionne à F_CPU/2 = 4MHz (avec SPI2X activé)
- L'écran ST7789 peut nécessiter jusqu'à 200ms après DISPON pour être prêt
- Certains écrans nécessitent un délai supplémentaire après le reset hardware
