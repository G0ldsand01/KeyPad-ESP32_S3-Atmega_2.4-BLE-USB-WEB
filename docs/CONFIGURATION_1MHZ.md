# Configuration ATmega328P à 1 MHz

## Modifications Appliquées

### 1. **F_CPU = 1 MHz**

Le code ATmega a été configuré pour fonctionner à 1 MHz au lieu de 8 MHz.

**Avantages** :
- Consommation réduite (~80% de réduction)
- Moins de chaleur générée
- Plus stable pour les applications à faible consommation

**Inconvénients** :
- Performance réduite (8x plus lent)
- Baudrate UART limité

### 2. **Baudrate UART réduit à 9600**

Avec F_CPU = 1 MHz, le baudrate maximum fiable est ~62500, mais 9600 est plus stable.

**Calcul** :
```
UBRR = (1000000 / 16 / 9600) - 1 = 5.51 ≈ 6
Erreur: ~0.16% (acceptable)
```

**Modifications** :
- ATmega : `#define UART_BAUD 9600`
- ESP32 : `ATMEGA_UART_BAUD = 9600`

### 3. **Désactivation des Rafraîchissements Automatiques**

Pour éviter les animations de refresh :

**Désactivé** :
- ✅ Rafraîchissement automatique basé sur la variation de lumière
- ✅ Rafraîchissement automatique lors du changement de backlight
- ✅ Mise à jour périodique de l'affichage

**Activé uniquement** :
- ✅ Affichage lors de la réception de `CMD_SET_DISPLAY_DATA` via UART
- ✅ Affichage initial au démarrage

### 4. **Délais Ajustés**

Avec F_CPU = 1 MHz, tous les délais sont maintenant 8x plus longs.

**Modifications** :
- Boucle principale : `_delay_ms(50)` au lieu de `_delay_ms(20)`
- Compteurs ajustés pour maintenir les mêmes intervalles réels

## Configuration des Fuses pour 1 MHz

Pour utiliser l'ATmega à 1 MHz, vous avez deux options :

### Option A : Oscillateur RC 8 MHz avec Division par 8

**Fuses** :
- **LOW**: `0xE2` → Int. RC Osc. 8 MHz
- **LOW.CKDIV8** : **COCHER** (activer la division par 8)
- Résultat : 8 MHz / 8 = 1 MHz

**Avantages** :
- Pas besoin de changer les fuses actuelles
- Juste cocher CKDIV8

**Nouvelle valeur LOW fuse** : `0xE2` avec CKDIV8 = 1 → `0xE3` (en binaire : 1110 0011)

### Option B : Oscillateur RC 1 MHz Direct

**Fuses** :
- **LOW**: Changer `CKSEL` pour utiliser l'oscillateur RC calibré à 1 MHz
- Plus complexe, nécessite de changer plusieurs bits

**Recommandation** : Utiliser l'Option A (CKDIV8) car plus simple.

## Modifications dans MPLAB IPE

### Pour activer CKDIV8 (Division par 8) :

1. **Ouvrir MPLAB IPE**
2. **Aller dans "Fuses"**
3. **Dans LOW fuse** :
   - **CKDIV8** : **COCHER** ✅
4. **Programmer les nouvelles fuses**

**Nouvelle valeur LOW fuse** : `0xE3` (au lieu de `0xE2`)

## Vérification

Après avoir modifié les fuses et recompilé le firmware :

1. **Vérifier F_CPU dans le code** : `#define F_CPU 1000000UL`
2. **Vérifier le baudrate** : `#define UART_BAUD 9600`
3. **Vérifier le baudrate ESP32** : `ATMEGA_UART_BAUD = 9600`
4. **Flasher le nouveau firmware**
5. **Tester la communication UART**

## Performance

Avec F_CPU = 1 MHz :

- **UART** : 9600 bauds (stable)
- **SPI** : ~500 kHz (F_CPU/2 avec SPI2X)
- **PWM** : ~4 kHz (F_CPU/256)
- **ADC** : Plus lent mais toujours fonctionnel
- **Affichage** : Plus lent mais sans animations

## Notes Importantes

⚠️ **ATTENTION** :
- Le baudrate UART est maintenant 9600 au lieu de 115200
- L'ESP32 doit être configuré avec le même baudrate (9600)
- Les délais sont maintenant 8x plus longs
- L'affichage ne se rafraîchit plus automatiquement (seulement via UART)

✅ **Avantages** :
- Consommation réduite
- Pas d'animations de refresh
- Plus stable
- Moins de chaleur

## Prochaines Étapes

1. **Modifier les fuses** : Cocher CKDIV8 dans MPLAB IPE
2. **Recompiler le firmware ATmega** avec F_CPU = 1000000UL
3. **Flasher le nouveau firmware**
4. **Redémarrer l'ESP32** (il utilisera automatiquement 9600 bauds)
5. **Tester la communication**
