# boot.py - Démarre automatiquement au boot de l'ESP32
# Ce fichier s'exécute automatiquement au démarrage

import machine
import time
import os

print("=" * 50)
print("Booting Macropad...")
print("=" * 50)

# Attendre un peu pour que le système soit prêt
time.sleep_ms(500)

# Vérifier si main.py existe et l'exécuter
try:
    files = os.listdir()
    if 'main.py' in files:
        print("[BOOT] Found main.py, starting application...")
        # Importer et exécuter main.py
        try:
            import main
            # Si main.py a été importé (pas exécuté directement), 
            # la clause else dans main.py devrait démarrer automatiquement
            # Mais pour être sûr, on peut aussi appeler main.main() explicitement
            if hasattr(main, 'main'):
                main.main()
            print("[BOOT] Main application started")
        except Exception as e:
            print("[BOOT] Error starting main:", e)
            import sys
            sys.print_exception(e)
    else:
        print("[BOOT] main.py not found in filesystem")
        print("[BOOT] Available files:", files)
        print("[BOOT] Please upload main.py to the ESP32")
        print("[BOOT] The device will not function without main.py")
except Exception as e:
    print("[BOOT] Error checking files:", e)
    import sys
    sys.print_exception(e)
