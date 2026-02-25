# Connexion USB ESP32-S3 — Guide

## Garder le port série connecté

### 1. Paramètres Arduino IDE

Dans **Outils** (Tools) :

- **USB CDC On Boot** : **Enabled** (obligatoire pour voir le Serial)
- **USB Mode** : **Hardware CDC and JTAG** (pour ESP32-S3 DevKit avec USB-JTAG)
- **Carte** : **ESP32S3 Dev Module**

### 2. Utiliser le moniteur série intégré (Web UI)

Le Web UI inclut un **moniteur série** dans l’onglet **Paramètres**. Une fois connecté en USB :

1. Connectez-vous via **Connecter (USB)**
2. Ouvrez l’onglet **Paramètres**
3. La sortie `Serial.println()` de l’ESP32 s’affiche dans la zone « Moniteur série »

**Avantage** : pas besoin d’Arduino IDE ni de maintenir le bouton BOOT. Une seule application utilise le port.

### 3. Conflit avec Arduino IDE / moniteur série

Un port série ne peut être ouvert que par **une seule** application à la fois :

- Si le **Web UI** est connecté → fermez le moniteur série de l’Arduino IDE
- Si l’**Arduino IDE** utilise le port → déconnectez-vous du Web UI avant d’ouvrir le moniteur série

### 4. Si la connexion se coupe (« device lost »)

- Utilisez un **câble USB data** (certains câbles ne font que la charge)
- Branchez directement sur un **port USB du PC**, pas via un hub
- Vérifiez l’**alimentation** (5 V stable)
- Sur certaines cartes : un **jumper** désactive le reset automatique (DTR→EN) — voir la doc de la carte

### 5. Bouton BOOT

Le bouton **BOOT** est nécessaire **uniquement pour flasher** (mettre à jour le firmware). Pour la communication série normale, vous n’avez pas besoin de le maintenir.
