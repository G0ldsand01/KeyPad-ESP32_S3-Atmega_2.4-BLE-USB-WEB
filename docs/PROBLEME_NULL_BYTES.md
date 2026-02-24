# Problème : ESP32 Reçoit Uniquement des 0x00

## 🔴 Symptôme

Dans les logs ESP32, tu vois uniquement :
```
[ATMEGA RX] Received 1 bytes: 00 | ASCII: .
[ATMEGA RX] Received 1 bytes: 00 | ASCII: .
```

Cela signifie que l'ESP32 reçoit uniquement des **null bytes (0x00)** au lieu de données réelles de l'ATmega.

## 🔍 Causes Possibles

### 1. Ligne RX Flottante (Pas de Signal)
- La ligne GPIO11 (RX ESP32) n'est pas correctement connectée
- Le diviseur de tension n'est pas installé ou mal connecté
- La ligne reste à 0V (LOW) → l'ESP32 lit toujours `0x00`

### 2. Diviseur de Tension Non Fonctionnel
- Les résistances ne sont pas correctement connectées
- Les valeurs de résistances sont incorrectes
- Le point de jonction n'est pas connecté à GPIO11

### 3. ATmega Ne Fonctionne Pas
- L'ATmega n'est pas alimenté (pas de 5V)
- L'ATmega n'a pas le firmware flashé
- L'ATmega ne démarre pas correctement

### 4. Câblage Incorrect
- GPIO11 n'est pas connecté au diviseur
- Le diviseur est sur GPIO10 au lieu de GPIO11
- Les GND ne sont pas connectés ensemble

## ✅ Solutions

### Étape 1 : Vérifier le Câblage

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

**Vérifications :**
- [ ] GPIO10 (ESP32 TX) → RX ATmega (PD0, Pin 2) - **direct**
- [ ] GPIO11 (ESP32 RX) ← TX ATmega (PD1, Pin 3) - **avec diviseur**
- [ ] Diviseur : R1=2.2kΩ entre ATmega TX et point de jonction
- [ ] Diviseur : R2=3.3kΩ entre point de jonction et GND
- [ ] Point de jonction → GPIO11 (ESP32 RX)
- [ ] GND ESP32 connecté à GND ATmega

### Étape 2 : Vérifier le Diviseur de Tension

Avec un multimètre, mesure la tension au **point de jonction** (entre R1 et R2) :

- **Quand ATmega TX = HIGH (5V)** → devrait mesurer **~3.0-3.3V**
- **Quand ATmega TX = LOW (0V)** → devrait mesurer **~0V**

Si tu mesures toujours **0V**, le problème est :
- Le diviseur n'est pas connecté correctement
- L'ATmega n'envoie rien (pas alimenté ou ne fonctionne pas)

### Étape 3 : Vérifier l'Alimentation de l'ATmega

- Vérifie que l'ATmega reçoit bien **5V stable**
- Vérifie que le GND de l'ATmega est connecté
- Vérifie que l'ATmega démarre (LED, écran, etc.)

### Étape 4 : Vérifier le Firmware ATmega

- Assure-toi que l'ATmega a bien le firmware `atmega_light` flashé
- Vérifie que l'ATmega démarre correctement (écran s'allume, etc.)

### Étape 5 : Test de Communication Bidirectionnelle

**Test 1 : ESP32 → ATmega (TX fonctionne)**
- Tu vois `[UART] Sent command 0x05...` ✅
- Cela signifie que l'ESP32 TX fonctionne

**Test 2 : ATmega → ESP32 (RX ne fonctionne pas)**
- Tu vois seulement `[ATMEGA RX] Received 1 bytes: 00` ❌
- Cela signifie que l'ESP32 RX ne reçoit pas de données valides

## 🔧 Corrections Apportées au Code

1. **Filtrage des null bytes** : Les bytes `0x00` purs sont maintenant ignorés pour réduire le spam
2. **Pull-up sur RX** : Ajout d'un pull-up interne sur GPIO11 pour éviter les lignes flottantes
3. **Messages d'aide** : Ajout de messages de diagnostic dans les logs

## 📋 Checklist de Diagnostic

- [ ] Vérifier que GPIO11 est bien connecté au diviseur (pas GPIO10)
- [ ] Vérifier que le diviseur est correctement installé (R1=2.2kΩ, R2=3.3kΩ)
- [ ] Mesurer la tension au point de jonction (devrait être ~3V quand ATmega TX = HIGH)
- [ ] Vérifier que l'ATmega est alimenté (5V)
- [ ] Vérifier que les GND sont connectés ensemble
- [ ] Vérifier que l'ATmega a le firmware flashé
- [ ] Redémarrer les deux microcontrôleurs après corrections

## 🚨 Si Ça Ne Fonctionne Toujours Pas

1. **Test sans diviseur** (temporaire, pour tester) :
   - Connecte directement GPIO11 à TX ATmega (sans diviseur)
   - ⚠️ **ATTENTION** : Cela peut endommager l'ESP32 si l'ATmega envoie 5V !
   - Si ça fonctionne, le problème est le diviseur
   - Si ça ne fonctionne pas, le problème est ailleurs

2. **Vérifier avec oscilloscope/logic analyzer** :
   - Observer le signal sur GPIO11
   - Vérifier que le signal arrive bien depuis l'ATmega

3. **Vérifier les résistances** :
   - Mesurer R1 et R2 avec un multimètre
   - Vérifier qu'elles ont les bonnes valeurs

4. **Test de continuité** :
   - Vérifier que toutes les connexions sont bonnes (pas de coupure)

## 📝 Notes

- Les `0x00` peuvent aussi être causés par un problème de baudrate, mais c'est moins probable
- Si tu vois des données mais corrompues (pas que des 0x00), c'est probablement un problème de diviseur ou de baudrate
- Le pull-up ajouté sur GPIO11 devrait aider, mais ne résoudra pas le problème si le diviseur n'est pas installé
