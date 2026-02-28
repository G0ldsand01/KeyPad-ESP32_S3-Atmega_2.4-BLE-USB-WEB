# Macropad — Pavé numérique configurable

Projet de pavé numérique personnalisable avec ESP32-S3 et ATmega328P, configurable via une interface web moderne (Web Serial / Web Bluetooth).

## 📁 Structure du projet

```
Projet_Final/
├── README.md                 # Ce fichier — vue d'ensemble
├── docs/
│   └── FONCTIONNALITES.md    # Description détaillée des fonctions
├── firmware/                 # Code Arduino (ESP32) et C++ (ATmega)
│   └── README.md             # Guide firmware
├── PCB/                      # Schémas et circuits imprimés KiCad
│   └── README.md             # Documentation PCB
├── public/
│   ├── scripts/main.js       # Logique principale de l'interface
│   └── styles/global.css     # Styles CSS
├── src/
│   ├── components/           # NumpadGrid, ConfigPanel, BacklightPanel, SettingsPanel, etc.
│   ├── layouts/
│   └── pages/
├── package.json
└── astro.config.mjs
```

## 🚀 Démarrage rapide

### Interface web

```bash
pnpm install
pnpm dev
```

Ouvrez `http://localhost:4321`

### Connexion au Macropad

- **USB** : Sélectionnez "Wired (USB)" → Connecter → Choisir le port série
- **Bluetooth** : Sélectionnez "Bluetooth (ESP32)" → Connecter → Choisir l'appareil

### Firmware

Voir [firmware/README.md](./firmware/README.md) pour compiler et flasher l'ESP32 et l'ATmega.

## ✅ Ce qui fonctionne actuellement

| Fonctionnalité | Statut |
|----------------|--------|
| Configuration des touches (simple, modificateur, média, macro) | ✅ |
| Profils multiples avec stockage flash ESP32 | ✅ |
| Connexion USB (Web Serial) | ✅ |
| Connexion Bluetooth (Web Bluetooth) | ✅ |
| Rétro-éclairage (luminosité, auto) | ✅ |
| Encodeur rotatif (volume, mute) | ✅ |
| OTA sans fil (mise à jour firmware via BLE/USB) | ✅ |
| Export/Import configuration JSON | ✅ |
| Moniteur série intégré | ✅ |
| Thème sombre/clair | ✅ |
| Matrice 5×4 (20 touches) | ✅ |

## ⚠️ À corriger / à améliorer

| Élément | Description |
|---------|-------------|
| Dongle 2.4 GHz | Support WiFi/2.4 GHz non implémenté |
| Capteur d'empreinte | Composant DisplayPanel/FingerprintPanel présent mais non intégré aux onglets actifs |
| Écran OLED | Gestion côté firmware (ATmega) ; interface web partielle |
| Vérifier mises à jour | Bouton "Vérifier les mises à jour" (placeholder) |
| Compatibilité Firefox/Safari | Web Serial et Web Bluetooth non supportés |
| Tests automatisés | Pas de suite de tests |

## 📋 À faire

- [ ] Intégrer les onglets Display et Fingerprint si matériel connecté
- [ ] Implémenter la vérification des mises à jour OTA
- [ ] Support dongle 2.4 GHz (WiFi)
- [ ] Tests E2E pour l'interface web
- [ ] Documentation des APIs JSON pour développeurs

## 🛠️ Technologies

- **Frontend** : Astro 4, CSS, Lucide Icons
- **Communication** : Web Serial API, Web Bluetooth API
- **Firmware** : Arduino (ESP32-S3), C++ (ATmega328P)
- **PCB** : KiCad

## 📚 Documentation

- [Fonctionnalités détaillées](./docs/FONCTIONNALITES.md)
- [Firmware Arduino/ESP](./firmware/README.md)
- [PCB](./PCB/README.md)

## 📄 Licence

Projet académique — Cégep Gerald-Godin.
