# Diviseur de Tension pour UART ESP32 ↔ ATmega328P

## 🔌 Problème

L'ESP32-S3 fonctionne en **3.3V** alors que l'ATmega328P fonctionne en **5V**. 

- ✅ **ESP32 TX (3.3V) → ATmega RX** : Pas de problème, l'ATmega accepte 3.3V comme niveau HIGH
- ⚠️ **ATmega TX (5V) → ESP32 RX** : **DANGER !** L'ESP32 ne peut accepter que 3.3V max, le 5V peut l'endommager

## ✅ Solution : Diviseur de Tension

Il faut réduire le signal 5V de l'ATmega TX à 3.3V avant qu'il n'arrive sur l'ESP32 RX.

### Schéma du Circuit

```
ATmega328P (5V)                    ESP32-S3 (3.3V)
     TX (PD1, Pin 3)                    RX (GPIO 11)
         |                                    |
         |                                    |
    [R1: 2.2kΩ]                               |
         |                                    |
         +----[R2: 3.3kΩ]----+                |
         |                    |               |
         |                    |               |
        GND                  GND              |
                                         [ESP32 RX]

```

### Calcul des Résistances

Pour obtenir **3.3V** à partir de **5V** :

```
Vout = Vin × (R2 / (R1 + R2))
3.3V = 5V × (R2 / (R1 + R2))

R2 / (R1 + R2) = 3.3 / 5 = 0.66

Si R1 = 2.2kΩ et R2 = 3.3kΩ :
Vout = 5V × (3.3kΩ / (2.2kΩ + 3.3kΩ))
Vout = 5V × (3.3 / 5.5)
Vout = 5V × 0.6 = 3.0V ≈ 3.3V (acceptable)
```

### Valeurs Recommandées

| R1 (kΩ) | R2 (kΩ) | Vout (V) | Note |
|---------|---------|----------|------|
| 2.2     | 3.3     | ~3.0     | ✅ Bon compromis |
| 1.8     | 3.3     | ~3.2     | ✅ Proche de 3.3V |
| 2.0     | 3.0     | ~3.0     | ✅ Valeurs standard |
| 1.0     | 2.0     | ~3.3     | ✅ Parfait mais courant plus élevé |

**Recommandation : R1 = 2.2kΩ, R2 = 3.3kΩ** (valeurs standard faciles à trouver)

### Schéma de Câblage Détaillé

```
                    ┌─────────────┐
                    │  ATmega328P │
                    │   (5V)      │
                    │             │
                    │  PD1 (TX)   │───┐
                    │  Pin 3      │   │
                    └─────────────┘   │
                                      │
                                      │ Signal 5V
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    │            [R1: 2.2kΩ]            │
                    │                 │                 │
                    │                 │                 │
                    │                 │                 │
                    │            [R2: 3.3kΩ]            │
                    │                 │                 │
                    │                 │                 │
                    │                 │                 │
                    │                GND                │
                    │                 │                 │
                    │                 │                 │
                    │                 │ Signal 3.3V     │
                    │                 │                 │
                    │                 ▼                 │
                    │         ┌──────────────┐          │
                    │         │  ESP32-S3    │          │
                    │         │   (3.3V)     │          │
                    │         │              │          │
                    └─────────┤ GPIO11 (RX)  │──────────┘
                              │              │
                              └──────────────┘
```

### Connexions Physiques

1. **ATmega TX (PD1, Pin 3)** → **R1 (2.2kΩ)** → **Point de jonction**
2. **Point de jonction** → **R2 (3.3kΩ)** → **GND** (masse commune)
3. **Point de jonction** → **ESP32 RX (GPIO 11)**

**Important :** Les deux GND (ATmega et ESP32) doivent être connectés ensemble (masse commune).

### Composants Nécessaires

- **R1** : Résistance 2.2kΩ (1/4W ou plus)
- **R2** : Résistance 3.3kΩ (1/4W ou plus)
- **Breadboard** ou **PCB** pour les connexions

### Alternative : Résistances SMD

Si tu utilises un PCB, tu peux utiliser des résistances SMD :
- **R1** : 2201 (2.2kΩ) ou 2200
- **R2** : 3301 (3.3kΩ) ou 3300
- Format : 0805 ou 0603 (faciles à souder)

### Vérification

Après avoir installé le diviseur :

1. **Mesurer la tension** au point de jonction (entre R1 et R2) :
   - Quand ATmega TX = HIGH (5V) → devrait mesurer ~3.0-3.3V
   - Quand ATmega TX = LOW (0V) → devrait mesurer ~0V

2. **Tester la communication UART** :
   - L'ESP32 devrait recevoir correctement les données de l'ATmega
   - Les logs `[ATMEGA]` devraient apparaître dans la console ESP32

### ⚠️ Notes Importantes

1. **Pas besoin de diviseur dans l'autre sens** : ESP32 TX (3.3V) → ATmega RX fonctionne directement car l'ATmega accepte 3.3V comme HIGH.

2. **Masse commune obligatoire** : Les GND des deux microcontrôleurs doivent être connectés ensemble.

3. **Protection supplémentaire** (optionnelle) : Tu peux ajouter une diode Schottky (ex: 1N5817) entre le point de jonction et l'ESP32 RX pour une protection supplémentaire :
   ```
   Point de jonction → Anode → Cathode → ESP32 RX
   ```

4. **Courant** : Avec R1=2.2kΩ et R2=3.3kΩ, le courant est faible (~1mA), donc pas de problème de dissipation.

### Schéma Simplifié (Vue d'Ensemble)

```
ESP32-S3                    Diviseur de Tension              ATmega328P
(3.3V)                                                          (5V)
  │                                                               │
  │                                                               │
TX│GPIO 10 ────────────────────────────────────────────────► RX│PD0 (Pin 2)
  │                                                               │
  │                                                               │
RX│GPIO 11 ◄───[R2: 3.3kΩ]───[Point]───[R1: 2.2kΩ]────────── TX│PD1 (Pin 3)
  │                │            │                                 │
  │                │            │                                 │
GND│───────────────┴────────────┴──────────────────────────── GND │
  │                                                               │
  └───────────────────────────────────────────────────────────────┘
```

### Exemple de Code de Test

Pour vérifier que le diviseur fonctionne, tu peux utiliser ce code sur l'ESP32 :

```python
from machine import Pin, UART
import time

# Initialiser UART
uart = UART(1, baudrate=115200, tx=Pin(10), rx=Pin(11))

while True:
    if uart.any():
        data = uart.read()
        print("[RX]", data)
    time.sleep(0.1)
```

Si tu vois des données dans `[RX]`, le diviseur fonctionne correctement !
