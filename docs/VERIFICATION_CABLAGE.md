# Vérification du Câblage UART

## 🔍 Diagnostic Actuel

Tu vois dans les logs ESP32 :
```
[UART] Sent command 0x05 (36 bytes total, 36 written)
```

Mais **AUCUN** log `[ATMEGA]` n'apparaît, ce qui signifie :
- ✅ **ESP32 TX fonctionne** (les commandes sont envoyées)
- ❌ **ESP32 RX ne fonctionne PAS** (l'ATmega n'envoie rien ou l'ESP32 ne reçoit rien)

## ✅ Câblage Correct

```
ESP32-S3                          ATmega328P
  │                                   │
TX│GPIO 10 ──────────────────────► RX│PD0 (Pin 2)
  │        [DIRECT, pas de résistance]│
  │                                   │
RX│GPIO 11 ◄───[Diviseur]──────── TX│PD1 (Pin 3)
  │        [R1=2.2kΩ + R2=3.3kΩ]     │
GND│─────────────────────────────── GND│
```

## ⚠️ Problème Probable

Tu mentionnes avoir un diviseur sur **GPIO10**, mais :
- **GPIO10** = TX de l'ESP32 (sortie) → **PAS besoin de diviseur**
- **GPIO11** = RX de l'ESP32 (entrée) → **BESOIN d'un diviseur**

## 🔧 Solution

### Étape 1 : Vérifier les Pins

Sur ton ESP32, vérifie :
- **GPIO10** est connecté au **RX de l'ATmega (PD0, Pin 2)** → **DIRECT, pas de résistance**
- **GPIO11** est connecté au **TX de l'ATmega (PD1, Pin 3)** → **AVEC diviseur R1=2.2kΩ + R2=3.3kΩ**

### Étape 2 : Installer le Diviseur sur GPIO11

Si tu as le diviseur sur GPIO10, **déplace-le sur GPIO11** :

```
ATmega TX (PD1, Pin 3) ──[R1: 2.2kΩ]──┐
                                      │─── GPIO 11 (ESP32 RX)
GND ─────────────────────[R2: 3.3kΩ]─┘
```

### Étape 3 : Vérifier la Masse Commune

Les deux GND (ESP32 et ATmega) doivent être connectés ensemble.

## 🧪 Test Après Correction

Après avoir corrigé le câblage, redémarre l'ESP32 et tu devrais voir :

```
[UART] ATmega UART initialized TX=10, RX=11, 115200 baud
[MAIN] Enabling ATmega debug...
[UART] Sent command 0x0A (2 bytes total, 2 written)
[UART] Sent command 0x0B (2 bytes total, 2 written)
[ATMEGA] === ATmega328P Light Controller ===
[ATMEGA] Command received: 0x0A (2 bytes)
[ATMEGA] Command received: 0x05 (36 bytes)
[ATMEGA] Received display data: profile=Profile 1, keys=17, last_key=/, time=10:36:16
```

Et l'écran devrait se mettre à jour avec :
- Le profil
- Le mode de connexion
- La dernière touche appuyée
- **L'heure actuelle** (au lieu de "--:--:--")

## 📋 Checklist

- [ ] GPIO10 (ESP32 TX) → RX ATmega (PD0) - **direct, pas de résistance**
- [ ] GPIO11 (ESP32 RX) ← TX ATmega (PD1) - **avec diviseur R1=2.2kΩ + R2=3.3kΩ**
- [ ] GND ESP32 connecté à GND ATmega
- [ ] Diviseur correctement installé sur **GPIO11 uniquement**
- [ ] Redémarrer l'ESP32 après correction
- [ ] Vérifier les logs `[ATMEGA]` dans la console

## 🚨 Si Ça Ne Fonctionne Toujours Pas

1. **Vérifier avec un multimètre** :
   - Mesurer la tension sur GPIO11 quand l'ATmega envoie HIGH (5V) → devrait être ~3.0-3.3V
   - Mesurer la tension sur GPIO11 quand l'ATmega envoie LOW (0V) → devrait être ~0V

2. **Vérifier que l'ATmega est bien alimenté** (5V stable)

3. **Vérifier que l'ATmega a bien le firmware flashé** (atmega_light avec les modifications récentes)

4. **Tester la communication dans l'autre sens** :
   - Envoyer une commande simple depuis l'ESP32 (ex: CMD_READ_LIGHT)
   - Vérifier si l'ATmega répond (devrait envoyer 2 bytes)
