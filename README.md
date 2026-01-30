# Interface Web de Configuration Macropad

Interface web moderne développée avec Astro pour configurer un pavé numérique/macropad personnalisable avec ESP32-S3 et ATmega328P/168A.

## 🎯 Fonctionnalités

- **Configuration des touches** : Assignation de touches simples, modificateurs, médias et macros
- **Profils multiples** : Création et gestion de plusieurs profils de configuration
- **Stockage persistant** : Les profils sont stockés dans la mémoire flash de l'ESP32 (transfert entre appareils)
- **Rétro-éclairage** : Contrôle de la luminosité avec ajustement automatique selon l'ambiance
- **Capteur d'empreinte digitale** : Déverrouillage biométrique de l'ordinateur
- **Écran OLED** : Affichage d'informations (batterie, mode de connexion, chanson en cours, etc.)
- **Potentiomètre rotatif** : Contrôle du volume par incréments
- **Connexions multiples** : USB-C (wired), Bluetooth et 2.4GHz USB dongle (futur)

## 🛠️ Technologies

- **Framework** : Astro 4.0+
- **Styling** : CSS moderne avec thème sombre/clair
- **Icônes** : Lucide Icons (CDN)
- **Communication** : Web Serial API, Web Bluetooth API

## 📦 Installation

### Prérequis

- Node.js 18+ et npm/pnpm/yarn
- Navigateur moderne (Chrome/Edge recommandé pour Web Serial et Web Bluetooth)

### Installation des dépendances

```bash
npm install
# ou
pnpm install
# ou
yarn install
```

## 🚀 Développement

### Démarrer le serveur de développement

```bash
npm run dev
# ou
pnpm dev
# ou
yarn dev
```

L'interface sera accessible à `http://localhost:4321`

### Build de production

```bash
npm run build
```

Les fichiers seront générés dans le dossier `dist/`

### Prévisualiser le build

```bash
npm run preview
```

## 📁 Structure du projet

```
.
├── public/
│   ├── scripts/
│   │   └── main.js          # Logique principale de l'application
│   └── styles/
│       └── global.css       # Styles CSS complets
├── src/
│   ├── components/          # Composants Astro
│   │   ├── BacklightPanel.astro
│   │   ├── ConfigPanel.astro
│   │   ├── DisplayPanel.astro
│   │   ├── FingerprintPanel.astro
│   │   ├── NumpadGrid.astro
│   │   ├── StatusBar.astro
│   │   └── TabNavigation.astro
│   ├── layouts/
│   │   └── Layout.astro    # Layout principal
│   └── pages/
│       └── index.astro     # Page principale
├── astro.config.mjs        # Configuration Astro
├── package.json
└── README.md
```

## 🔌 Connexion au Macropad

### USB (Wired)

1. Connectez l'ESP32-S3 via USB-C
2. Sélectionnez "Wired (USB)" dans le menu déroulant
3. Cliquez sur "Se connecter"
4. Sélectionnez le port série dans la liste (ex: COM3, /dev/ttyUSB0)

### Bluetooth

1. Assurez-vous que l'ESP32 est en mode Bluetooth
2. Sélectionnez "Bluetooth (ESP32)" dans le menu déroulant
3. Cliquez sur "Se connecter"
4. Sélectionnez l'appareil "Macropad" dans la liste Bluetooth

### WiFi (Futur)

Le support WiFi sera activé avec le dongle USB 2.4GHz.

## 📝 Configuration

### Profils

- Créez plusieurs profils pour différents usages
- Chaque profil peut avoir ses propres configurations de touches
- Le profil "Configuration" contient les touches de navigation partagées
- **Les profils sont stockés dans la mémoire flash de l'ESP32** : vous pouvez déplacer votre macropad entre différents appareils et conserver vos configurations
- Les profils sont sauvegardés automatiquement lors de chaque modification

### Types de touches

- **Touche simple** : Touche unique (ex: A, ENTER, SPACE)
- **Modificateur** : CTRL, SHIFT, ALT, GUI/WIN
- **Média** : Volume, Play/Pause, Suivant, Précédent
- **Macro** : Séquence de touches avec délai configurable

### Rétro-éclairage

- Luminosité manuelle : 0-100%
- Ajustement automatique selon la luminosité ambiante
- Contrôle via capteur de lumière

### Écran OLED

- Mode données : Affichage d'informations système
- Mode image : Image statique personnalisée (128×64, 1-bit)
- Mode GIF : Animation GIF (jusqu'à 8 frames)

## 🔧 Matériel

### Composants principaux

- **Switches** : Gateron KS-33 Low Profile 2.0 (35 pièces)
- **Microcontrôleur principal** : ESP32-S3
- **Détection de touches** : ATmega328P ou ATmega168A
- **Écran** : OLED I2C 128×64 (8 broches)
- **Potentiomètre** : Encodeur rotatif infini
- **Capteur d'empreinte** : DFRobot SEN0348
- **Dongle 2.4GHz** : (Futur) Ensemble de connexion sans-fil USB

### Documentation matériel

- [README ESP32-S3](./docs/README_ESP32.md) - Configuration et librairies Arduino
- [README ATmega328P](./docs/README_ATmega328P.md) - Configuration et librairies Microchip Studio
- [Firmware](./firmware/README.md) - Code source des microcontrôleurs
- [Stockage des profils](./firmware/esp32/README_STOCKAGE.md) - Documentation du stockage persistant

## 🌐 Compatibilité navigateur

- **Chrome/Edge** : Support complet (Web Serial, Web Bluetooth)
- **Firefox** : Support partiel (pas de Web Serial)
- **Safari** : Support limité

## 📄 Licence

Ce projet est développé dans le cadre d'un projet académique.

## 👨‍💻 Développement

### Scripts disponibles

- `npm run dev` : Démarrer le serveur de développement
- `npm run build` : Construire pour la production
- `npm run preview` : Prévisualiser le build de production
- `npm run astro` : Commandes Astro CLI

### Notes de développement

- Les icônes Lucide sont chargées depuis CDN
- La configuration est sauvegardée dans `localStorage`
- Les communications série/bluetooth utilisent les APIs Web standard
- Le thème (sombre/clair) est sauvegardé dans `localStorage`

## 🐛 Dépannage

### Le port série n'apparaît pas

- Vérifiez que l'ESP32-S3 est bien connecté
- Installez les pilotes USB nécessaires
- Fermez Arduino IDE ou autres applications utilisant le port
- Utilisez Chrome ou Edge (Web Serial non supporté sur Firefox/Safari)

### Bluetooth ne fonctionne pas

- Vérifiez que le Bluetooth est activé sur votre ordinateur
- Assurez-vous que l'ESP32 est en mode Bluetooth
- Utilisez Chrome ou Edge (Web Bluetooth non supporté sur Firefox/Safari)

### Les icônes ne s'affichent pas

- Vérifiez votre connexion internet (CDN Lucide)
- Ouvrez la console du navigateur pour voir les erreurs
- Videz le cache du navigateur
