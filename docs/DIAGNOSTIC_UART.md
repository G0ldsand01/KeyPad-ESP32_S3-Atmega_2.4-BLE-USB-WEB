# Diagnostic UART ESP32 ↔ ATmega328P

## 🔴 Problème Actuel

Tu mentionnes :
- **GPIO9** connecté au **RX de l'ATmega**
- **GPIO10** (avec résistance 2k2) connecté au **TX de l'ATmega**

Mais le code ESP32 utilise :
- **GPIO10** = TX de l'ESP32 (doit aller vers RX de l'ATmega)
- **GPIO11** = RX de l'ESP32 (doit recevoir depuis TX de l'ATmega)

## ✅ Câblage Correct

### Connexions UART

```
ESP32-S3                          ATmega328P
(3.3V)                            (5V)
  │                                   │
TX│GPIO 10 ──────────────────────► RX│PD0 (Pin 2)
  │                                   │
  │                                   │
RX│GPIO 11 ◄───[Diviseur]──────── TX│PD1 (Pin 3)
  │                                   │
GND│─────────────────────────────── GND│
```

### Diviseur de Tension (OBLIGATOIRE)

Le diviseur de tension doit être sur **GPIO11 (RX de l'ESP32)** :

```
ATmega TX (PD1, Pin 3) ──[R1: 2.2kΩ]──┐
                                      │─── ESP32 RX (GPIO 11)
GND ─────────────────────[R2: 3.3kΩ]─┘
```

**⚠️ IMPORTANT :**
- **GPIO10 (TX ESP32)** → **PAS de diviseur** → **RX ATmega (PD0)**
- **GPIO11 (RX ESP32)** → **AVEC diviseur** → **TX ATmega (PD1)**

## 🔧 Correction du Câblage

### Étape 1 : Vérifier les Pins

1. **ESP32 TX (GPIO10)** doit aller directement au **RX de l'ATmega (PD0, Pin 2)**
   - Pas de résistance nécessaire ici (3.3V est accepté par l'ATmega)

2. **ESP32 RX (GPIO11)** doit recevoir depuis le **TX de l'ATmega (PD1, Pin 3)**
   - **AVEC diviseur de tension** : R1=2.2kΩ + R2=3.3kΩ

### Étape 2 : Corriger le Câblage

Si tu as actuellement :
- GPIO9 → RX ATmega ❌ (mauvais pin)
- GPIO10 → TX ATmega ❌ (mauvais pin + diviseur au mauvais endroit)

Tu dois changer pour :
- **GPIO10** → **RX ATmega (PD0, Pin 2)** ✅ (direct, pas de résistance)
- **GPIO11** → **TX ATmega (PD1, Pin 3)** ✅ (avec diviseur R1+R2)

### Étape 3 : Vérifier le Diviseur

Le diviseur doit être sur la ligne **GPIO11** (RX ESP32), pas sur GPIO10 :

```
ATmega TX (Pin 3) ──[2.2kΩ]──┐
                             │─── GPIO 11 (ESP32 RX)
GND ───────────────[3.3kΩ]──┘
```

## 🧪 Test de Diagnostic

Après avoir corrigé le câblage, redémarre l'ESP32 et tu devrais voir dans les logs :

```
[UART] ATmega UART initialized TX=10, RX=11, 115200 baud
[MAIN] Initializing ATmega UART...
[MAIN] ATmega UART initialized successfully
[MAIN] Waiting for ATmega to boot...
[MAIN] Enabling ATmega debug...
[UART] Enabling ATmega debug...
[UART] Sent command 0x0A (2 bytes total, 2 written)
[UART] Sent command 0x0B (2 bytes total, 2 written)
[UART] ATmega debug commands sent (level 3)
[MAIN] Sending initial display update...
[UART] Sending display update: profile=Profile 1, mode=data, keys=17, last_key=, time=00:00:15
[UART] Sent command 0x05 (XX bytes total, XX written)
[MAIN] Initial display update sent
```

Et ensuite, tu devrais voir des logs de l'ATmega :
```
[ATMEGA] === ATmega328P Light Controller ===
[ATMEGA] UART Baud: 115200
[ATMEGA] Boot sequence started...
[LIGHT] Level: 512 (0x0200)
```

## 🔍 Vérifications

### 1. Vérifier les Pins Physiques

Sur l'ESP32-S3, vérifie que :
- **GPIO10** est bien utilisé comme TX (sortie)
- **GPIO11** est bien utilisé comme RX (entrée)

### 2. Vérifier le Diviseur de Tension

Avec un multimètre, mesure la tension au point de jonction (entre R1 et R2) :
- Quand l'ATmega envoie HIGH (5V) → devrait mesurer **~3.0-3.3V**
- Quand l'ATmega envoie LOW (0V) → devrait mesurer **~0V**

### 3. Vérifier la Masse Commune

Les deux GND (ESP32 et ATmega) doivent être connectés ensemble.

### 4. Test de Communication Bidirectionnelle

Si tu vois les logs `[UART] Sent command...` mais pas de réponse `[ATMEGA]...`, cela signifie :
- ✅ L'ESP32 envoie bien (TX fonctionne)
- ❌ L'ESP32 ne reçoit pas (RX ne fonctionne pas) → **Vérifier le diviseur sur GPIO11**

## 📋 Checklist de Correction

- [ ] GPIO10 (ESP32 TX) → RX ATmega (PD0, Pin 2) - **direct, pas de résistance**
- [ ] GPIO11 (ESP32 RX) ← TX ATmega (PD1, Pin 3) - **avec diviseur R1=2.2kΩ + R2=3.3kΩ**
- [ ] GND ESP32 connecté à GND ATmega (masse commune)
- [ ] Diviseur de tension correctement installé sur GPIO11 uniquement
- [ ] Redémarrer l'ESP32 après correction
- [ ] Vérifier les logs dans la console

## 🚨 Si Ça Ne Fonctionne Toujours Pas

1. **Vérifier que l'ATmega est bien alimenté** (5V stable)
2. **Vérifier que l'ATmega a bien le firmware flashé** (atmega_light)
3. **Vérifier le baudrate** (115200 des deux côtés)
4. **Tester avec un oscilloscope/logic analyzer** si disponible
5. **Vérifier que GPIO9 n'est pas utilisé ailleurs** (c'est l'encodeur SW dans ton code)

## 📝 Notes

- **GPIO9** est utilisé pour l'encodeur rotatif (bouton SW) dans ton code, donc ne l'utilise pas pour l'UART
- Le diviseur de tension est **CRITIQUE** pour protéger l'entrée RX de l'ESP32
- Sans le diviseur, l'ESP32 peut être endommagé par le signal 5V de l'ATmega
