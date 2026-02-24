# Correction des Fuses - Oscillateur ATmega328P

## Problème identifié

L'ATmega328P utilise actuellement :
- **Oscillateur RC interne à 8 MHz** (selon les fuses)
- Mais le code était configuré pour **16 MHz**

Cela cause un **baudrate UART incorrect** !

## Solution appliquée

Le code a été modifié pour utiliser `F_CPU = 8000000UL` (8 MHz) au lieu de `16000000UL` (16 MHz).

## Calcul du baudrate

Avec F_CPU = 8 MHz et baudrate = 115200 :
```
UBRR = (8000000 / 16 / 115200) - 1 = 3.34 ≈ 3
```

Le code calcule maintenant correctement le baudrate pour 8 MHz.

## Options pour améliorer la précision

### Option 1 : Utiliser un oscillateur externe à 16 MHz (Recommandé)

**Avantages** :
- Plus précis et stable
- Meilleure performance
- Baudrate UART plus précis

**Modifications nécessaires** :
1. **Dans MPLAB IPE - Fuses** :
   - **LOW.SUT_CKSEL** : Changer pour "Ext. Crystal Osc. 8.0-16.0 MHz" ou "Ext. Clock"
   - **LOW fuse** : Devrait devenir `0xFF` ou `0xF7`
2. **Dans le code** : Remettre `F_CPU = 16000000UL`
3. **Hardware** : Connecter un cristal 16 MHz entre XTAL1 et XTAL2 avec des condensateurs de charge (typiquement 22pF)

### Option 2 : Utiliser l'oscillateur RC calibré (Actuel)

**Avantages** :
- Pas besoin de composants externes
- Fonctionne immédiatement

**Inconvénients** :
- Moins précis (±10% de tolérance)
- Peut dériver avec la température
- Baudrate peut avoir des erreurs

**Modifications nécessaires** :
- Le code a déjà été modifié pour 8 MHz ✅
- Recompiler et flasher le firmware

## Vérification

Après avoir appliqué la correction :

1. **Recompiler le firmware ATmega** avec `F_CPU = 8000000UL`
2. **Flasher le nouveau firmware**
3. **Redémarrer l'ESP32**
4. **Vérifier** que l'UART fonctionne maintenant

## Notes importantes

- L'oscillateur RC interne à 8 MHz a une précision d'environ ±10%
- Pour une meilleure précision UART, un oscillateur externe est recommandé
- Si vous changez d'oscillateur, n'oubliez pas de mettre à jour `F_CPU` dans le code !
