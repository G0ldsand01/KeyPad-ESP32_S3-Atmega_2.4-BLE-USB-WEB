    from machine import Pin, PWM, UART
    import time
    import bluetooth
    from micropython import const
    import struct
    import json
    import sys
    import uselect

    # Désactiver les messages de debug répétitifs en mode autonome
    # (pour économiser la mémoire et améliorer les performances)
    DEBUG_MODE = False  # Mettre à True pour plus de debug

    # Essayer d'importer USB HID si disponible
    try:
        import usb_hid
        from adafruit_hid.keyboard import Keyboard
        from adafruit_hid.keycode import Keycode
        from adafruit_hid.consumer_control import ConsumerControl
        from adafruit_hid.consumer_control_code import ConsumerControlCode
        USB_HID_AVAILABLE = True
        print("[USB] USB HID available")
    except ImportError:
        USB_HID_AVAILABLE = False
        print("[USB] USB HID not available - using BLE only")

    # =========================================================
    # CONFIG MATRICE
    # =========================================================

    COL_PINS = [16, 17, 18, 8]     # C0..C3
    ROW_PINS = [4, 5, 6, 7, 15]    # R0..R4

    cols = [Pin(p, Pin.OUT, value=1) for p in COL_PINS]
    rows = [Pin(p, Pin.IN, Pin.PULL_UP) for p in ROW_PINS]

    MATRIX_ROWS = len(ROW_PINS)
    MATRIX_COLS = len(COL_PINS)

    # KEYMAP - peut être modifié via web UI Astro
    KEYMAP = [
        ["PROFILE", "/", "*", "-"],   # R0
        ["7", "8", "9", "+"],         # R1
        ["4", "5", "6", None],        # R2
        ["1", "2", "3", "="],         # R3
        ["0", ".", None, None],       # R4
    ]

    DEBOUNCE_MS = 30
    last_state = [[0] * MATRIX_COLS for _ in range(MATRIX_ROWS)]
    last_change = [[0] * MATRIX_COLS for _ in range(MATRIX_ROWS)]

    # =========================================================
    # ENCODEUR ROTATIF (Volume Control) + UART ATMEGA
    # =========================================================

    # Pins de l'encodeur (selon config.h Arduino)
    ENCODER_CLK = 3   # GPIO 3 pour CLK
    ENCODER_DT = 46   # GPIO 46 pour DT
    ENCODER_SW = 9    # GPIO 9 pour bouton (SW)

    # UART vers ATmega328P (même câblage que le firmware Arduino)
    ATMEGA_UART_TX = 10    # ESP32 TX → ATmega RX
    ATMEGA_UART_RX = 11    # ESP32 RX → ATmega TX
    ATMEGA_UART_BAUD = 115200

    # Initialiser les pins de l'encodeur
    encoder_clk = Pin(ENCODER_CLK, Pin.IN, Pin.PULL_UP)
    encoder_dt = Pin(ENCODER_DT, Pin.IN, Pin.PULL_UP)
    encoder_sw = Pin(ENCODER_SW, Pin.IN, Pin.PULL_UP)

    # Variables pour l'encodeur
    encoder_last_state = encoder_clk.value()
    encoder_button_pressed = False
    last_encoder_read = 0
    last_button_press = 0
    encoder_position = 0  # Position relative pour compter les "crans" (detents)

    # UART ATmega
    atmega_uart = None
    atmega_rx_buffer = b""
    last_key_pressed = ""  # Dernière touche appuyée pour l'affichage ATmega

    # =========================================================
    # LED CONTROL (PWM)
    # =========================================================

    LED_PIN = 2  # GPIO2 pour LED (ajuste selon ton hardware)
    led_pwm = None
    led_brightness = 128  # 0-255
    backlight_enabled = True

    try:
        led_pwm = PWM(Pin(LED_PIN))
        led_pwm.freq(1000)
        led_pwm.duty(led_brightness)
        print("[LED] LED PWM initialized on GPIO", LED_PIN)
    except Exception as e:
        print("[LED] Error initializing LED:", e)

    def set_led_brightness(brightness):
        """Set LED brightness 0-255"""
        global led_brightness
        led_brightness = max(0, min(255, brightness))
        if led_pwm:
            duty = int(led_brightness * 1023 / 255)
            led_pwm.duty(duty)
        print("[LED] Brightness set to", led_brightness)

    # =========================================================
    # UART ATMEGA - LOGGING
    # =========================================================

    def init_atmega_uart():
        """Initialiser l'UART vers l'ATmega pour loguer toutes les données reçues"""
        global atmega_uart
        try:
            atmega_uart = UART(
                1,
                baudrate=ATMEGA_UART_BAUD,
                tx=Pin(ATMEGA_UART_TX),
                rx=Pin(ATMEGA_UART_RX),
            )
            print("[UART] ATmega UART initialized TX={}, RX={}, {} baud".format(
                ATMEGA_UART_TX, ATMEGA_UART_RX, ATMEGA_UART_BAUD))
        except Exception as e:
            print("[UART] Error initializing ATmega UART:", e)


    def read_atmega_uart():
        """Lire tout ce qui arrive de l'ATmega et le logger (console + web UI)"""
        global atmega_rx_buffer
        if not atmega_uart:
            return
        try:
            if not atmega_uart.any():
                return
            data = atmega_uart.read()
            if not data:
                return

            # Accumuler dans un buffer et traiter ligne par ligne
            atmega_rx_buffer += data
            while b"\n" in atmega_rx_buffer:
                line, atmega_rx_buffer = atmega_rx_buffer.split(b"\n", 1)
                if not line:
                    continue
                
                # Filtrer les lignes avec seulement des null bytes ou des espaces
                if line.strip(b"\x00\r\n\t ") == b"":
                    continue
                
                # 1) Essayer de décoder en ASCII lisible
                text = None
                try:
                    text = line.decode("utf-8").strip()
                except UnicodeDecodeError:
                    try:
                        text = line.decode("latin-1").strip()
                    except:
                        text = None

                # Ignorer les lignes vides ou avec seulement des caractères non imprimables
                if text and len(text.strip()) == 0:
                    continue
                
                # Filtrer les lignes avec seulement des null bytes
                if text and all(ord(c) == 0 for c in text):
                    continue

                # 2) Si c'est une chaîne hex pure (00..FF répété), la rendre lisible
                is_hex = False
                bytes_list = None
                if text is not None:
                    s = text.replace(" ", "").lower()
                    if len(s) % 2 == 0 and len(s) > 2 and all(c in "0123456789abcdef" for c in s):
                        try:
                            bytes_list = [int(s[i:i+2], 16) for i in range(0, len(s), 2)]
                            # Ignorer si c'est seulement des 00
                            if not all(b == 0 for b in bytes_list):
                                is_hex = True
                        except:
                            pass

                if is_hex and bytes_list is not None:
                    # Affichage lisible : hex groupé + vue ASCII
                    hex_groups = " ".join("{:02X}".format(b) for b in bytes_list)
                    ascii_view = "".join(chr(b) if 32 <= b < 127 else "." for b in bytes_list)
                    print("[ATMEGA HEX]", hex_groups)
                    print("[ATMEGA ASCII]", ascii_view)

                    # Vers Web UI
                    try:
                        msg = json.dumps({
                            "type": "atmega_log",
                            "mode": "hex",
                            "hex": hex_groups,
                            "ascii": ascii_view,
                        })
                        send_to_web(msg)
                    except:
                        pass
                elif text and len(text) > 0:
                    # Cas texte "normal" (ASCII) - seulement si non vide
                    print("[ATMEGA]", text)

                    try:
                        msg = json.dumps({
                            "type": "atmega_log",
                            "mode": "text",
                            "text": text,
                        })
                        send_to_web(msg)
                    except:
                        pass
        except Exception as e:
            if DEBUG_MODE:
                print("[UART] Error reading ATmega:", e)

    def send_display_update_to_atmega():
        """Envoyer les données d'affichage à l'ATmega via UART"""
        global last_key_pressed, atmega_uart
        if not atmega_uart:
            return
        
        try:
            # Format: [0x04][profile_len][profile][output_len][output][keys_count][last_key_len][last_key][backlight_enabled][backlight_brightness]
            profile = "Profile 1"  # TODO: récupérer le profil actuel depuis la config
            output_mode = "bluetooth" if BLE_AVAILABLE and ble_kbd and len(ble_kbd._conns) > 0 else "usb"
            keys_count = sum(1 for row in KEYMAP for key in row if key is not None)
            last_key = last_key_pressed if last_key_pressed else ""
            backlight_enabled = 1 if backlight_enabled else 0
            backlight_brightness = led_brightness
            
            # Construire le message
            msg = bytearray()
            msg.append(0x04)  # Commande DISPLAY_UPDATE
            
            # Profile
            profile_bytes = profile.encode('utf-8')
            msg.append(len(profile_bytes))
            msg.extend(profile_bytes)
            
            # Output mode
            output_bytes = output_mode.encode('utf-8')
            msg.append(len(output_bytes))
            msg.extend(output_bytes)
            
            # Keys count
            msg.append(keys_count)
            
            # Last key pressed
            last_key_bytes = last_key.encode('utf-8')
            msg.append(len(last_key_bytes))
            msg.extend(last_key_bytes)
            
            # Backlight enabled
            msg.append(backlight_enabled)
            
            # Backlight brightness
            msg.append(backlight_brightness)
            
            # Envoyer via UART
            atmega_uart.write(bytes(msg))
            
            if DEBUG_MODE:
                print("[UART] Sent display update to ATmega: profile={}, mode={}, keys={}, last_key={}".format(
                    profile, output_mode, keys_count, last_key))
        except Exception as e:
            if DEBUG_MODE:
                print("[UART] Error sending display update to ATmega:", e)

    # =========================================================
    # USB SERIAL (pour Web Serial API)
    # =========================================================

    # USB Serial - utiliser uselect pour lecture non-bloquante
    # Note: sys.stdin peut ne pas être disponible si pas de connexion USB série
    serial_buffer = ""
    poll = None
    try:
        poll = uselect.poll()
        if hasattr(sys.stdin, 'read'):
            poll.register(sys.stdin, uselect.POLLIN)
    except:
        pass

    def read_serial():
        """Lire depuis USB Serial (si disponible)"""
        global serial_buffer
        if poll is None:
            return
        try:
            # Vérifier si des données sont disponibles (non-bloquant)
            if poll.poll(0):
                if hasattr(sys.stdin, 'read'):
                    data = sys.stdin.read(1)
                    if data:
                        serial_buffer += data
                        if '\n' in serial_buffer:
                            lines = serial_buffer.split('\n')
                            serial_buffer = lines[-1]
                            for line in lines[:-1]:
                                if line.strip():
                                    process_web_message(line.strip())
        except:
            pass

    def write_serial(data):
        """Écrire vers USB Serial (si disponible)"""
        try:
            if hasattr(sys.stdout, 'write'):
                sys.stdout.write(data + '\n')
                sys.stdout.flush()
        except:
            # Pas de connexion série USB, c'est normal en mode autonome
            pass

    # =========================================================
    # BLE HID KEYBOARD + BLE SERIAL
    # =========================================================

    _IRQ_CENTRAL_CONNECT = const(1)
    _IRQ_CENTRAL_DISCONNECT = const(2)
    _IRQ_GATTS_WRITE = const(3)
    _IRQ_GET_SECRET = const(29)
    _IRQ_SET_SECRET = const(30)

    # UUIDs pour HID
    _UUID_HID = bluetooth.UUID(0x1812)
    _UUID_REPORT_MAP = bluetooth.UUID(0x2A4B)
    _UUID_REPORT = bluetooth.UUID(0x2A4D)
    _UUID_HID_INFO = bluetooth.UUID(0x2A4A)
    _UUID_PROTOCOL_MODE = bluetooth.UUID(0x2A4E)

    # UUIDs pour Serial Bluetooth (compatible avec Web Bluetooth API)
    _UUID_SERIAL_SERVICE = bluetooth.UUID("0000ffe0-0000-1000-8000-00805f9b34fb")
    _UUID_SERIAL_CHAR = bluetooth.UUID("0000ffe1-0000-1000-8000-00805f9b34fb")

    # HID Report Map avec Report IDs explicites
    HID_REPORT_MAP = bytes([
        # Keyboard Report (Report ID 0x01)
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
        0x85, 0x01,  # Report ID 0x01
        0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,
        0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
        0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
        0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
        0xC0,
        # Consumer Control Report (Report ID 0x02)
        0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
        0x85, 0x02,  # Report ID 0x02
        0x15, 0x00, 0x26, 0x9C, 0x02,
        0x75, 0x10, 0x95, 0x01, 0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2,
        0x81, 0x00, 0xC0,
    ])

    # Codes HID pour les touches numériques (clavier principal, pas numpad)
    # Compatible avec tous les systèmes (Windows/Android/macOS/iOS)
    KEYCODES = {
        "1": 0x1E, "2": 0x1F, "3": 0x20,  # 1, 2, 3
        "4": 0x21, "5": 0x22, "6": 0x23,  # 4, 5, 6
        "7": 0x24, "8": 0x25, "9": 0x26,  # 7, 8, 9
        "0": 0x27,  # 0
        "/": 0x38,   # Forward Slash (/)
        "*": 0x25,   # Asterisk (*) - utilise 8, on ajoutera Shift dans send_key
        "-": 0x2D,   # Minus (-)
        "+": 0x2E,   # Plus (+) - utilise Equals, on ajoutera Shift dans send_key
        ".": 0x37,   # Period (.)
        "=": 0x2E,   # Equals (=)
    }

    CONSUMER_CODES = {
        "VOL_UP": 0xE9,
        "VOL_DOWN": 0xEA,
        "MUTE": 0xE2,
    }

    class BleKeyboard:
        def __init__(self, name="Macropad"):
            print("[BLE] Initializing BLE...")
            try:
                self._ble = bluetooth.BLE()
                print("[BLE] BLE object created")
                self._ble.active(True)
                print("[BLE] BLE active:", self._ble.active())
                self._ble.irq(self._irq)
                print("[BLE] IRQ handler set")
            except Exception as e:
                print("[BLE] Error in __init__:", e)
                import sys
                sys.print_exception(e)
                raise

            # Service HID
            hid_info = (_UUID_HID_INFO, bluetooth.FLAG_READ)
            report_map = (_UUID_REPORT_MAP, bluetooth.FLAG_READ)
            proto_mode = (_UUID_PROTOCOL_MODE, bluetooth.FLAG_READ | bluetooth.FLAG_WRITE)
            report = (_UUID_REPORT, bluetooth.FLAG_READ | bluetooth.FLAG_NOTIFY)

            self._hid_service = (
                _UUID_HID,
                (hid_info, report_map, proto_mode, report),
            )

            # Service Serial Bluetooth (pour Web UI)
            serial_char = (_UUID_SERIAL_CHAR, bluetooth.FLAG_READ | bluetooth.FLAG_WRITE | bluetooth.FLAG_NOTIFY)
            self._serial_service = (
                _UUID_SERIAL_SERVICE,
                (serial_char,),
            )

            # Enregistrer les services (tous ensemble)
            try:
                print("[BLE] Registering services...")
                ((self._hid_info_handle,
                self._report_map_handle,
                self._proto_mode_handle,
                self._report_handle),
                (self._serial_char_handle,)) = self._ble.gatts_register_services((self._hid_service, self._serial_service))
                print("[BLE] Services registered successfully")
                print("[BLE] HID handles:", self._hid_info_handle, self._report_map_handle, self._proto_mode_handle, self._report_handle)
                print("[BLE] Serial handle:", self._serial_char_handle)
            except Exception as e:
                print("[BLE] Error registering services:", e)
                import sys
                sys.print_exception(e)
                raise
            
            # Pas besoin de report_cc_handle séparé - on utilisera le même report avec Report ID
            self._report_cc_handle = self._report_handle

            try:
                # HID Information (version, flags, country code)
                # Format: bcdVersion (2 bytes), flags (1 byte), countryCode (1 byte)
                # Version 1.1.1, flags 0x01 (RemoteWake), country code 0x00 (not localized)
                self._ble.gatts_write(self._hid_info_handle, struct.pack("<HHB", 0x0111, 0x0001, 0x00))
                self._ble.gatts_write(self._report_map_handle, HID_REPORT_MAP)
                # Protocol Mode: 0x01 = Report Protocol (requis pour HID)
                self._ble.gatts_write(self._proto_mode_handle, b"\x01")
                
                # Pour Windows, il est parfois nécessaire d'initialiser le report avec une valeur par défaut
                empty_report = bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
                self._ble.gatts_write(self._report_handle, empty_report)
                
                print("[BLE] HID characteristics configured")
            except Exception as e:
                print("[BLE] Error writing HID characteristics:", e)
                import sys
                sys.print_exception(e)

            self._conns = set()
            self._serial_conns = set()
            self._adv_payload = self._advertising_payload(name)
            self._last_keepalive = 0  # Pour le keep-alive Windows
            self._serial_buffer = {}  # Buffer pour accumuler les messages fragmentés par connexion
            
            print("[BLE] BLE initialized")
            # Attendre un peu avant de démarrer l'advertising
            time.sleep_ms(200)
            self.advertise()
            # Vérifier que l'advertising a démarré
            time.sleep_ms(100)
            print("[BLE] Advertising should be active now")

        def _irq(self, event, data):
            if event == _IRQ_CENTRAL_CONNECT:
                conn_handle, addr_type, addr = data
                self._conns.add(conn_handle)
                self._serial_conns.add(conn_handle)
                print("[BLE] Device connected! Handle:", conn_handle)
                print("[BLE] Address type:", addr_type, "Address:", bytes(addr).hex())
                
                # Windows a besoin de plus de temps pour établir la connexion HID
                # Attendre que la connexion soit complètement établie
                time.sleep_ms(500)
                
                # Envoyer un rapport vide pour "activer" la connexion HID
                # Windows vérifie souvent la conformité HID au moment de la connexion
                try:
                    empty_report = bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
                    # Écrire d'abord la valeur
                    self._ble.gatts_write(self._report_handle, empty_report)
                    time.sleep_ms(100)
                    # Envoyer la notification
                    self._ble.gatts_notify(conn_handle, self._report_handle, empty_report)
                    time.sleep_ms(100)
                    print("[BLE] HID connection activated for Windows")
                except Exception as e:
                    print("[BLE] Error activating HID:", e)
                    import sys
                    sys.print_exception(e)
            elif event == _IRQ_CENTRAL_DISCONNECT:
                conn_handle, _, _ = data
                self._conns.discard(conn_handle)
                self._serial_conns.discard(conn_handle)
                # Nettoyer le buffer de cette connexion
                if conn_handle in self._serial_buffer:
                    del self._serial_buffer[conn_handle]
                print("[BLE] Device disconnected, handle:", conn_handle)
                print("[BLE] Remaining connections:", len(self._conns))
                # Attendre un peu avant de redémarrer l'advertising
                time.sleep_ms(500)
                print("[BLE] Restarting advertising after disconnect...")
                self.advertise()
                print("[BLE] Advertising restarted")
            elif event == _IRQ_GET_SECRET:
                # Windows peut demander des secrets pour le pairing
                # Retourner None pour désactiver le pairing (pas de PIN)
                return None
            elif event == _IRQ_SET_SECRET:
                # Windows peut essayer de définir des secrets
                # Retourner True pour accepter
                return True
            elif event == _IRQ_GATTS_WRITE:
                conn_handle, value_handle = data
                if value_handle == self._serial_char_handle:
                    # Message reçu via Bluetooth Serial
                    value = self._ble.gatts_read(value_handle)
                    try:
                        # Accumuler les fragments dans le buffer
                        if conn_handle not in self._serial_buffer:
                            self._serial_buffer[conn_handle] = b''
                        
                        self._serial_buffer[conn_handle] += value
                        
                        # Essayer de décoder et traiter les messages complets
                        buffer = self._serial_buffer[conn_handle]
                        
                        # Chercher les messages complets (séparés par \n)
                        while b'\n' in buffer:
                            line, buffer = buffer.split(b'\n', 1)
                            if line:
                                try:
                                    message = line.decode('utf-8').strip()
                                    if message:
                                        process_web_message(message)
                                except UnicodeDecodeError:
                                    # Ignorer les fragments incomplets
                                    pass
                                except Exception as e:
                                    if DEBUG_MODE:
                                        print("[BLE] Error processing message:", e)
                        
                        # Garder le reste dans le buffer
                        self._serial_buffer[conn_handle] = buffer
                        
                    except Exception as e:
                        if DEBUG_MODE:
                            print("[BLE] Error reading serial:", e)

        def advertise(self):
            try:
                # Arrêter l'advertising actuel si actif
                try:
                    self._ble.gap_advertise(None)
                    time.sleep_ms(200)  # Attendre un peu plus longtemps
                except:
                    pass
                
                # Advertising data (max 31 bytes)
                adv_data = self._adv_payload
                
                # Scan response data pour Serial Service UUID (max 31 bytes)
                # Serial Service UUID (128-bit)
                serial_uuid_bytes = bytes.fromhex("fb349b5f800010008000e0ff0000")
                scan_resp = bytes([0x11, 0x07]) + serial_uuid_bytes
                
                print("[BLE] Advertising payload length:", len(adv_data))
                print("[BLE] Scan response length:", len(scan_resp))
                
                # Syntaxe correcte pour MicroPython: gap_advertise(interval_us, adv_data=None, *, connectable=True)
                # scan_resp n'est pas supporté directement, on utilise seulement adv_data
                advertising_started = False
                try:
                    # Syntaxe standard MicroPython
                    self._ble.gap_advertise(100_000, adv_data=adv_data, connectable=True)
                    print("[BLE] Advertising as 'Macropad'...")
                    advertising_started = True
                except (TypeError, ValueError) as e1:
                    try:
                        # Syntaxe alternative: paramètres positionnels
                        self._ble.gap_advertise(100_000, adv_data, True)
                        print("[BLE] Advertising as 'Macropad' (positional)...")
                        advertising_started = True
                    except Exception as e2:
                        print("[BLE] Error starting advertising!")
                        print("[BLE] Error 1:", e1)
                        print("[BLE] Error 2:", e2)
                        import sys
                        sys.print_exception(e2)
                
                if advertising_started:
                    print("[BLE] Advertising started successfully")
                    print("[BLE] Device should be discoverable as 'Macropad'")
                else:
                    print("[BLE] WARNING: Advertising may not have started!")
                    
            except Exception as e:
                print("[BLE] Error advertising:", e)
                import sys
                sys.print_exception(e)

        def _advertising_payload(self, name):
            name_bytes = name.encode()
            name_len = len(name_bytes) + 1
            
            # Flags (3 bytes)
            flags = bytes([0x02, 0x01, 0x06])
            
            # Complete Local Name (max 29 bytes total pour advertising data)
            name_data = bytes([name_len, 0x09]) + name_bytes
            
            # HID Service UUID (16-bit) - 4 bytes
            hid_uuid = bytes([0x03, 0x03, 0x12, 0x18])  # 0x1812 en little-endian
            
            # Limiter la taille du payload (max 31 bytes pour advertising data)
            # On va mettre seulement le HID UUID dans advertising, Serial sera dans scan response
            payload = flags + name_data + hid_uuid
            
            # Vérifier la taille
            if len(payload) > 31:
                # Tronquer le nom si nécessaire
                max_name_len = 31 - len(flags) - len(hid_uuid) - 1  # -1 pour le byte de longueur
                if max_name_len > 0:
                    name_bytes = name_bytes[:max_name_len-1]
                    name_len = len(name_bytes) + 1
                    name_data = bytes([name_len, 0x09]) + name_bytes
                    payload = flags + name_data + hid_uuid
            
            return payload

        def send_report(self, keycode=0, modifier=0):
            if not self._conns:
                return
            # Report ID 0x01 pour keyboard (premier report dans le report map)
            # Format: Report ID (1 byte) + modifier (1 byte) + reserved (1 byte) + 6 keycodes (6 bytes)
            report = bytes([0x01, modifier, 0x00, keycode, 0x00, 0x00, 0x00, 0x00, 0x00])
            # Utiliser une copie de la liste pour éviter les modifications pendant l'itération
            for conn in list(self._conns):
                try:
                    # Écrire d'abord la valeur dans la caractéristique
                    self._ble.gatts_write(self._report_handle, report)
                    time.sleep_ms(10)  # Délai un peu plus long pour Windows
                    # Envoyer la notification
                    self._ble.gatts_notify(conn, self._report_handle, report)
                    time.sleep_ms(5)
                except Exception as e:
                    # Si la connexion a été fermée, la retirer de la liste
                    error_str = str(e).lower()
                    if "not connected" in error_str or "invalid" in error_str or "eagain" in error_str:
                        self._conns.discard(conn)
                        self._serial_conns.discard(conn)
                        print("[BLE] Connection lost, removed from list")
                    elif DEBUG_MODE:
                        print("[HID] Error sending report:", e)

        def send_consumer_control(self, code):
            if not self._conns:
                return
            # Consumer Control report avec Report ID 0x02 (défini dans le report map)
            # Format: Report ID (1 byte) + Consumer Control code (2 bytes little-endian)
            report = bytes([0x02]) + struct.pack("<H", code)
            for conn in self._conns:
                try:
                    # Utiliser le même handle que le report keyboard
                    self._ble.gatts_write(self._report_handle, report)
                    time.sleep_ms(5)
                    self._ble.gatts_notify(conn, self._report_handle, report)
                except Exception as e:
                    print("[HID] Error sending Consumer Control:", e)
            time.sleep_ms(50)
            # Relâcher
            report_release = bytes([0x02, 0x00, 0x00])
            for conn in self._conns:
                try:
                    self._ble.gatts_write(self._report_handle, report_release)
                    time.sleep_ms(5)
                    self._ble.gatts_notify(conn, self._report_handle, report_release)
                except:
                    pass

        def press(self, symbol):
            kc = KEYCODES.get(symbol)
            if kc is None:
                print("[HID] Unknown keycode for:", symbol)
                return
            
            # Pour + et *, on doit utiliser Shift
            modifier = 0
            if symbol == "+":
                modifier = 0x02  # Left Shift
                kc = KEYCODES["="]  # Utilise Equals comme base
            elif symbol == "*":
                modifier = 0x02  # Left Shift
                kc = KEYCODES["8"]  # Utilise 8 comme base
            
            self.send_report(kc, modifier)

        def release(self):
            self.send_report(0)
        
        def volume_up(self):
            self.send_consumer_control(CONSUMER_CODES["VOL_UP"])
        
        def volume_down(self):
            self.send_consumer_control(CONSUMER_CODES["VOL_DOWN"])
        
        def mute(self):
            self.send_consumer_control(CONSUMER_CODES["MUTE"])
        
        def send_serial(self, data):
            """Envoyer des données via Bluetooth Serial"""
            if not self._serial_conns:
                return
            try:
                data_bytes = (data + '\n').encode('utf-8')
                self._ble.gatts_write(self._serial_char_handle, data_bytes)
                for conn in self._serial_conns:
                    try:
                        self._ble.gatts_notify(conn, self._serial_char_handle, data_bytes)
                    except:
                        pass
            except Exception as e:
                print("[BLE] Error sending serial:", e)


    # Initialiser BLE Keyboard
    BLE_AVAILABLE = False
    try:
        ble_kbd = BleKeyboard("Macropad")
        BLE_AVAILABLE = True
        print("[BLE] BLE keyboard initialized")
    except Exception as e:
        print("[BLE] Error initializing BLE:", e)
        ble_kbd = None

    # Initialiser USB HID si disponible
    usb_keyboard = None
    usb_consumer = None
    if USB_HID_AVAILABLE:
        try:
            usb_keyboard = Keyboard(usb_hid.devices)
            usb_consumer = ConsumerControl(usb_hid.devices)
            print("[USB] USB HID keyboard initialized")
        except Exception as e:
            print("[USB] Error initializing USB HID:", e)
            USB_HID_AVAILABLE = False

    # =========================================================
    # TRAITEMENT DES MESSAGES WEB UI
    # =========================================================

    def process_web_message(message):
        """Traiter les messages JSON de l'interface web Astro"""
        try:
            # Ignorer les messages vides ou trop courts
            if not message or len(message) < 2:
                return
            
            # Essayer de parser le JSON
            try:
                data = json.loads(message)
            except ValueError:
                # JSON invalide - probablement un fragment, ignorer silencieusement
                if DEBUG_MODE:
                    print("[WEB] Invalid JSON (fragment?):", message[:50])
                return
            
            msg_type = data.get("type")
            
            if msg_type == "config":
                handle_config_message(data)
            elif msg_type == "backlight":
                handle_backlight_message(data)
            elif msg_type == "display":
                handle_display_message(data)
            elif msg_type == "get_config":
                send_config_to_web()
            elif msg_type == "get_light":
                send_light_level()
            elif msg_type == "status":
                send_status_message("Macropad ready")
            elif msg_type == "ota_start":
                handle_ota_start(data)
            elif msg_type == "ota_chunk":
                handle_ota_chunk(data)
            elif msg_type == "ota_end":
                handle_ota_end(data)
            else:
                if DEBUG_MODE:
                    print("[WEB] Unknown message type:", msg_type)
        except Exception as e:
            # Ne pas spammer les erreurs pour les fragments
            if DEBUG_MODE or "ota" not in str(message).lower():
                print("[WEB] Error processing message:", e)

    def handle_config_message(data):
        """Traiter la configuration des touches"""
        global KEYMAP
        
        if "keys" in data:
            # Convertir les clés de format "row-col" vers KEYMAP
            keys = data["keys"]
            for key_id, key_config in keys.items():
                try:
                    row, col = map(int, key_id.split("-"))
                    if 0 <= row < MATRIX_ROWS and 0 <= col < MATRIX_COLS:
                        value = key_config.get("value", "")
                        if value:
                            KEYMAP[row][col] = value
                        else:
                            KEYMAP[row][col] = None
                except:
                    pass
        
        print("[WEB] Keymap updated")
        send_status_message("Configuration updated")

    def handle_backlight_message(data):
        """Traiter la configuration du rétro-éclairage"""
        global backlight_enabled, led_brightness
        
        if "enabled" in data:
            backlight_enabled = data["enabled"]
            if not backlight_enabled and led_pwm:
                led_pwm.duty(0)
            elif backlight_enabled and led_pwm:
                set_led_brightness(led_brightness)
        
        if "brightness" in data:
            set_led_brightness(data["brightness"])
        
        print("[WEB] Backlight config updated")
        send_status_message("Backlight config updated")

    def handle_display_message(data):
        """Traiter la configuration de l'écran"""
        # Pour l'instant, juste logger
        print("[WEB] Display config:", data)
        send_status_message("Display config updated")

    def send_config_to_web():
        """Envoyer la configuration actuelle à l'interface web"""
        keys = {}
        for r in range(MATRIX_ROWS):
            for c in range(MATRIX_COLS):
                if KEYMAP[r][c]:
                    keys[f"{r}-{c}"] = {
                        "type": "key",
                        "value": KEYMAP[r][c]
                    }
        
        config = {
            "type": "config",
            "rows": MATRIX_ROWS,
            "cols": MATRIX_COLS,
            "keys": keys,
            "activeProfile": "Profil 1",
            "profiles": {"Profil 1": {"keys": keys}},
            "outputMode": "bluetooth"
        }
        
        send_to_web(json.dumps(config))

    def send_light_level():
        """Envoyer le niveau de lumière ambiante"""
        # Pour l'instant, valeur fictive (à remplacer par lecture capteur)
        light_level = 500
        response = json.dumps({"type": "light", "level": light_level})
        send_to_web(response)

    def send_status_message(message):
        """Envoyer un message de statut"""
        response = json.dumps({"type": "status", "message": message})
        send_to_web(response)

    # =========================================================
    # OTA UPDATE SUPPORT
    # =========================================================

    # Variables globales pour OTA
    ota_in_progress = False
    ota_file = None
    ota_chunk_count = 0
    ota_total_chunks = 0
    ota_file_size = 0

    def handle_ota_start(data):
        """Démarrer une mise à jour OTA"""
        global ota_in_progress, ota_file, ota_chunk_count, ota_total_chunks, ota_file_size
        
        try:
            ota_file_size = data.get("size", 0)
            ota_total_chunks = data.get("chunks", 0)
            filename = data.get("filename", "main.py")
            
            if ota_in_progress:
                send_status_message("OTA already in progress")
                return
            
            # Créer un fichier temporaire pour la mise à jour
            temp_filename = filename + ".tmp"
            try:
                # Supprimer l'ancien fichier temporaire s'il existe
                import os
                if temp_filename in os.listdir():
                    os.remove(temp_filename)
            except:
                pass
            
            ota_file = open(temp_filename, "w")
            ota_in_progress = True
            ota_chunk_count = 0
            
            print("[OTA] Starting update: {} ({} bytes, {} chunks)".format(
                filename, ota_file_size, ota_total_chunks))
            send_status_message("OTA: Starting update...")
            
            # Confirmer le début
            response = {
                "type": "ota_status",
                "status": "started",
                "message": "OTA update started"
            }
            send_to_web(json.dumps(response))
            
        except Exception as e:
            print("[OTA] Error starting OTA:", e)
            ota_in_progress = False
            if ota_file:
                ota_file.close()
                ota_file = None
            send_status_message("OTA error: " + str(e))

    def handle_ota_chunk(data):
        """Recevoir un chunk de données OTA"""
        global ota_file, ota_chunk_count
        
        if not ota_in_progress or not ota_file:
            send_status_message("OTA: No update in progress")
            return
        
        try:
            chunk_data = data.get("data", "")
            chunk_index = data.get("index", -1)
            
            # Décoder les données base64 si nécessaire
            if data.get("encoded", False):
                import ubinascii
                chunk_data = ubinascii.a2b_base64(chunk_data).decode('utf-8')
            
            # Écrire le chunk dans le fichier
            ota_file.write(chunk_data)
            ota_file.flush()
            ota_chunk_count += 1
            
            # Envoyer la progression
            progress = int((ota_chunk_count / ota_total_chunks) * 100) if ota_total_chunks > 0 else 0
            response = {
                "type": "ota_status",
                "status": "progress",
                "progress": progress,
                "chunk": ota_chunk_count,
                "total": ota_total_chunks
            }
            send_to_web(json.dumps(response))
            
            if DEBUG_MODE:
                print("[OTA] Received chunk {}/{} ({}%)".format(
                    ota_chunk_count, ota_total_chunks, progress))
            
        except Exception as e:
            print("[OTA] Error receiving chunk:", e)
            send_status_message("OTA error: " + str(e))
            # Nettoyer en cas d'erreur
            ota_in_progress = False
            if ota_file:
                ota_file.close()
                ota_file = None

    def handle_ota_end(data):
        """Finaliser la mise à jour OTA"""
        global ota_in_progress, ota_file, ota_chunk_count, ota_total_chunks
        
        if not ota_in_progress or not ota_file:
            send_status_message("OTA: No update in progress")
            return
        
        try:
            import os
            import machine
            
            # Fermer le fichier temporaire
            if ota_file:
                try:
                    ota_file.close()
                except:
                    pass
            ota_file = None
            
            # Vérifier que tous les chunks ont été reçus
            if ota_chunk_count < ota_total_chunks:
                send_status_message("OTA: Incomplete update ({} < {})".format(
                    ota_chunk_count, ota_total_chunks))
                # Supprimer le fichier temporaire
                try:
                    os.remove("main.py.tmp")
                except:
                    pass
                ota_in_progress = False
                return
            
            # Vérifier la taille du fichier
            temp_filename = "main.py.tmp"
            file_size = os.stat(temp_filename)[6]  # st_size
            
            if file_size != ota_file_size:
                send_status_message("OTA: Size mismatch ({} != {})".format(
                    file_size, ota_file_size))
                try:
                    os.remove(temp_filename)
                except:
                    pass
                ota_in_progress = False
                return
            
            # Sauvegarder l'ancien main.py
            try:
                if "main.py" in os.listdir():
                    os.rename("main.py", "main.py.bak")
            except:
                pass
            
            # Remplacer main.py par le nouveau fichier
            os.rename(temp_filename, "main.py")
            
            print("[OTA] Update completed successfully!")
            send_status_message("OTA: Update completed! Restarting...")
            
            # Envoyer la confirmation
            response = {
                "type": "ota_status",
                "status": "completed",
                "message": "Update completed, restarting..."
            }
            send_to_web(json.dumps(response))
            
            # Attendre un peu pour que le message soit envoyé
            time.sleep_ms(500)
            
            # Redémarrer l'ESP32
            machine.reset()
            
        except Exception as e:
            print("[OTA] Error finalizing update:", e)
            import sys
            sys.print_exception(e)
            send_status_message("OTA error: " + str(e))
            ota_in_progress = False
            if ota_file:
                try:
                    ota_file.close()
                except:
                    pass
                ota_file = None

    def send_keypress(row, col):
        """Envoyer un événement de pression de touche"""
        response = json.dumps({"type": "keypress", "row": row, "col": col})
        send_to_web(response)

    def send_to_web(data):
        """Envoyer des données à l'interface web (USB Serial ou Bluetooth Serial)"""
        write_serial(data)
        if BLE_AVAILABLE and ble_kbd:
            ble_kbd.send_serial(data)

    # =========================================================
    # GESTION DES TOUCHES
    # =========================================================

    def send_key(symbol, pressed, row=None, col=None):
        if symbol is None:
            return

        # Envoyer l'événement à l'interface web
        if row is not None and col is not None:
            send_keypress(row, col)

        if symbol == "PROFILE":
            if pressed:
                print("[PROFILE] switch profile")
            return
        
        if symbol == "VOL_UP":
            if pressed:
                print("VOLUME UP")
                if USB_HID_AVAILABLE and usb_consumer:
                    usb_consumer.send(ConsumerControlCode.VOLUME_INCREMENT)
                elif BLE_AVAILABLE and ble_kbd:
                    ble_kbd.volume_up()
            return
        
        if symbol == "VOL_DOWN":
            if pressed:
                print("VOLUME DOWN")
                if USB_HID_AVAILABLE and usb_consumer:
                    usb_consumer.send(ConsumerControlCode.VOLUME_DECREMENT)
                elif BLE_AVAILABLE and ble_kbd:
                    ble_kbd.volume_down()
            return
        
        if symbol == "MUTE":
            if pressed:
                print("MUTE")
                if USB_HID_AVAILABLE and usb_consumer:
                    usb_consumer.send(ConsumerControlCode.MUTE)
                elif BLE_AVAILABLE and ble_kbd:
                    ble_kbd.mute()
            return

        # Envoyer via USB HID si disponible, sinon BLE
        if pressed:
            global last_key_pressed
            last_key_pressed = symbol  # Mettre à jour la dernière touche appuyée
            print("DOWN:", symbol)
            kc = KEYCODES.get(symbol)
            if kc is None:
                return
            
            # D'abord envoyer la touche à l'appareil (HID)
            if USB_HID_AVAILABLE and usb_keyboard:
                # Envoyer via USB HID
                try:
                    # Mapping pour les touches numériques (clavier principal)
                    keycode_map = {
                        0x1E: Keycode.ONE, 0x1F: Keycode.TWO, 0x20: Keycode.THREE,
                        0x21: Keycode.FOUR, 0x22: Keycode.FIVE, 0x23: Keycode.SIX,
                        0x24: Keycode.SEVEN, 0x25: Keycode.EIGHT, 0x26: Keycode.NINE,
                        0x27: Keycode.ZERO,
                        0x38: Keycode.FORWARD_SLASH, 0x2D: Keycode.MINUS,
                        0x37: Keycode.PERIOD, 0x2E: Keycode.EQUALS,
                    }
                    adafruit_key = keycode_map.get(kc)
                    if adafruit_key:
                        # Pour + et *, on doit utiliser Shift
                        if symbol == "+":
                            usb_keyboard.press(Keycode.SHIFT, Keycode.EQUALS)
                            time.sleep_ms(10)
                            usb_keyboard.release(Keycode.SHIFT, Keycode.EQUALS)
                        elif symbol == "*":
                            usb_keyboard.press(Keycode.SHIFT, Keycode.EIGHT)
                            time.sleep_ms(10)
                            usb_keyboard.release(Keycode.SHIFT, Keycode.EIGHT)
                        else:
                            usb_keyboard.press(adafruit_key)
                            time.sleep_ms(10)
                            usb_keyboard.release(adafruit_key)
                        print("[USB] Key sent:", symbol)
                    else:
                        print("[USB] Unknown keycode:", hex(kc))
                except Exception as e:
                    print("[USB] Error sending key:", e)
            elif BLE_AVAILABLE and ble_kbd:
                # Envoyer via BLE HID
                ble_kbd.press(symbol)
            else:
                print("[HID] No HID available! USB:", USB_HID_AVAILABLE, "BLE:", BLE_AVAILABLE)
            
            # Ensuite envoyer la mise à jour d'affichage à l'ATmega (pour afficher la touche sur l'écran)
            send_display_update_to_atmega()
        else:
            print("UP  :", symbol)
            # USB HID relâche automatiquement après press
            if BLE_AVAILABLE and ble_kbd:
                ble_kbd.release()

    def scan_matrix():
        now = time.ticks_ms()

        for c in range(MATRIX_COLS):
            for i, col_pin in enumerate(cols):
                col_pin.value(0 if i == c else 1)

            time.sleep_us(50)

            for r in range(MATRIX_ROWS):
                raw = rows[r].value()
                pressed = 1 if raw == 0 else 0

                if pressed != last_state[r][c]:
                    if time.ticks_diff(now, last_change[r][c]) > DEBOUNCE_MS:
                        last_change[r][c] = now
                        last_state[r][c] = pressed

                        symbol = KEYMAP[r][c] if c < len(KEYMAP[r]) else None
                        send_key(symbol, pressed, r, c)

        for col_pin in cols:
            col_pin.value(1)

    # =========================================================
    # GESTION ENCODEUR ROTATIF
    # =========================================================

    def handle_encoder():
        """Gérer l'encodeur rotatif pour volume up/down et mute"""
        global encoder_last_state, encoder_button_pressed, last_encoder_read, last_button_press, encoder_position
        
        now = time.ticks_ms()
        
        # --- Gestion de la rotation ---
        # On lit l'encodeur assez souvent, mais on applique un petit délai anti-rebond.
        if time.ticks_diff(now, last_encoder_read) > 2:  # 2 ms mini entre lectures
            clk_state = encoder_clk.value()
            dt_state = encoder_dt.value()
            
            # On ne s'intéresse qu'aux changements de CLK
            if clk_state != encoder_last_state:
                # Quadrature simple :
                # - Si DT != CLK → un sens
                # - Si DT == CLK → l'autre sens
                if dt_state != clk_state:
                    encoder_position += 1   # Par ex. volume UP
                else:
                    encoder_position -= 1   # Par ex. volume DOWN
                
                last_encoder_read = now
            
            encoder_last_state = clk_state
        
        # Quand on a accumulé assez de "pas" (transitions), on déclenche un vrai step de volume.
        # La plupart des encodeurs génèrent 4 transitions par cran, mais 2 suffisent déjà pour
        # avoir un comportement "un cran = un step".
        if encoder_position >= 2:
            adjust_volume(1)
            encoder_position = 0
        elif encoder_position <= -2:
            adjust_volume(-1)
            encoder_position = 0
        
        # --- Gestion du bouton (mute) ---
        sw_state = encoder_sw.value()
        if sw_state == 0 and not encoder_button_pressed and time.ticks_diff(now, last_button_press) > 200:
            encoder_button_pressed = True
            last_button_press = now
            toggle_mute()
        elif sw_state == 1:
            encoder_button_pressed = False

    def adjust_volume(direction):
        """Ajuster le volume (direction > 0 = up, < 0 = down)"""
        if direction > 0:
            # Volume up
            if USB_HID_AVAILABLE and usb_consumer:
                usb_consumer.send(ConsumerControlCode.VOLUME_INCREMENT)
            elif BLE_AVAILABLE and ble_kbd:
                ble_kbd.volume_up()
            print("[ENCODER] Volume UP")
        else:
            # Volume down
            if USB_HID_AVAILABLE and usb_consumer:
                usb_consumer.send(ConsumerControlCode.VOLUME_DECREMENT)
            elif BLE_AVAILABLE and ble_kbd:
                ble_kbd.volume_down()
            print("[ENCODER] Volume DOWN")

    def toggle_mute():
        """Basculer le mute"""
        if USB_HID_AVAILABLE and usb_consumer:
            usb_consumer.send(ConsumerControlCode.MUTE)
        elif BLE_AVAILABLE and ble_kbd:
            ble_kbd.mute()
        print("[ENCODER] MUTE toggle")
        time.sleep_ms(30)  # Délai pour éviter les répétitions trop rapides

    # =========================================================
    # MAIN LOOP
    # =========================================================

    def main():
        print("=" * 50)
        print("Macropad Ready")
        print("=" * 50)
        print("[USB] Connect via Web Serial API")
        print("[BLE] Connect via Web Bluetooth API")
        print("[WEB] Use interface: https://key-pad-esp-32-s3-atmega-2-4-ble-us.vercel.app/")
        print("[ENCODER] Rotary encoder initialized (GPIO 3, 46, 9)")
        print("=" * 50)
        
        # Initialiser l'UART vers l'ATmega (pour log complet UART)
        init_atmega_uart()
        
        # Envoyer la mise à jour d'affichage initiale à l'ATmega
        time.sleep_ms(500)  # Attendre que l'ATmega soit prêt
        send_display_update_to_atmega()
        
        # Keep-alive pour Windows (envoie un rapport vide toutes les 2 secondes)
        last_keepalive = time.ticks_ms()
        
        # Compteur pour la mise à jour périodique de l'affichage ATmega
        last_display_update = time.ticks_ms()
        
        while True:
            scan_matrix()
            read_serial()
            handle_encoder()  # Gérer l'encodeur rotatif
            read_atmega_uart()  # Logger tout ce qui vient de l'ATmega
            
            now = time.ticks_ms()
            
            # Mettre à jour l'affichage ATmega périodiquement (toutes les 2 secondes)
            # pour s'assurer que les informations sont à jour même si aucune touche n'est pressée
            if time.ticks_diff(now, last_display_update) > 2000:  # Toutes les 2 secondes
                send_display_update_to_atmega()
                last_display_update = now
            
            # Keep-alive pour Windows - envoie un rapport vide périodiquement
            # pour maintenir la connexion active
            if BLE_AVAILABLE and ble_kbd and ble_kbd._conns:
                if time.ticks_diff(now, last_keepalive) > 2000:  # Toutes les 2 secondes
                    try:
                        # Envoyer un rapport vide pour maintenir la connexion
                        empty_report = bytes([0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
                        for conn in list(ble_kbd._conns):
                            try:
                                ble_kbd._ble.gatts_write(ble_kbd._report_handle, empty_report)
                                ble_kbd._ble.gatts_notify(conn, ble_kbd._report_handle, empty_report)
                            except:
                                # Connexion perdue, sera nettoyée lors du prochain envoi
                                pass
                        last_keepalive = now
                    except:
                        pass
            
            time.sleep_ms(5)


    # Démarrer automatiquement si exécuté directement ou depuis boot.py
    if __name__ == "__main__":
        main()
    else:
        # Si importé depuis boot.py, démarrer aussi
        try:
            main()
        except KeyboardInterrupt:
            print("\n[INFO] Stopped by user")
        except Exception as e:
            print("[ERROR] Fatal error:", e)
            import sys
            sys.print_exception(e)