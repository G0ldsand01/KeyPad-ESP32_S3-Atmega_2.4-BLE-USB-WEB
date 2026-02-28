# PCB Macropad

Circuits imprimés et schémas du pavé numérique, conçus avec KiCad.

> Voir le [README principal](../README.md) pour la vue d'ensemble du projet.

## 📁 Fichiers

| Fichier | Description |
|---------|-------------|
| `numberpad.kicad_pro` | Projet KiCad |
| `numberpad.kicad_pcb` | Carte PCB |
| `numberpad.kicad_sch` | Schéma électrique |
| `LED.kicad_sch` | Sous-schéma LED |
| `Switch.kicad_sch` | Sous-schéma switch |
| `bom/ibom.html` | BOM interactif |

## 🔧 Composants principaux

- **ESP32-S3-WROOM-2** : Microcontrôleur principal (16 MB Flash, 8 MB PSRAM)
- **ATmega328P** : Scan matrice touches (optionnel, selon version)
- **Switches** : Gateron KS-33 Low Profile 2.0
- **Encodeur rotatif** : Volume + bouton mute
- **LED** : NeoPixel ou rétro-éclairage
- **Connecteurs** : USB-C, écran, capteur empreinte

## 📐 Spécifications

- **Format** : À définir selon version (voir fichiers KiCad)
- **Couches** : 2 (typique pour prototype)
- **Finition** : À préciser pour fabrication

## 🛠️ Ouverture du projet

1. Installer [KiCad](https://www.kicad.org/)
2. Ouvrir `numberpad.kicad_pro`
3. Schéma : `numberpad.kicad_sch`
4. PCB : `numberpad.kicad_pcb`

## 📦 Fabrication

1. Exporter les fichiers Gerber depuis KiCad (Fichier > Plot)
2. Exporter le fichier de perçage (Excellon)
3. Envoyer à un fabricant (JLCPCB, PCBWay, etc.)

## ⚠️ Notes

- Vérifier les empreintes (footprints) avant fabrication
- Le BOM interactif (`bom/ibom.html`) aide à la pose des composants
- Adapter les chemins des modèles 3D si nécessaire
