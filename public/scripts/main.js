// Configuration du macropad
let config = {
    rows: 5,
    cols: 4,
    profiles: { 'Profil 1': { keys: {} }, 'Configuration': { keys: {} } },
    activeProfile: 'Profil 1',
    outputMode: 'usb',
    backlight: {
        enabled: true,
        brightness: 128,
        autoBrightness: false,
        envBrightness: false
    },
    fingerprint: {
        enabled: false,
        fingerprints: [], // Liste des empreintes: [{id, name, profileId, enrolledDate, lastUsed}]
        maxFingerprints: 10
    },
    settings: {
        debug: {
            esp32Enabled: false,
            esp32LogLevel: 'info',
            atmegaEnabled: false,
            atmegaLogLevel: 'info',
            hid: false,
            i2c: false,
            web: false,
            display: false,
            config: false
        },
        logging: {
            esp32Enabled: false,
            atmegaEnabled: false
        }
    },
    display: {
        brightness: 128,
        mode: 'data',
        imageData: null,
        gifFrames: [],
        customData: {
            showProfile: true,
            showBattery: true,
            showMode: true,
            showKeys: true,
            showBacklight: true,
            showCustom1: false,
            showCustom2: false,
            customLine1: '',
            customLine2: ''
        }
    },
    connected: false,
    connectionType: null,
    serialPort: null,
    bluetoothDevice: null,
    bluetoothServer: null,
    bluetoothCharacteristic: null
};

let selectedKey = null;
let ws = null;
let reconnectAttempts = 0;
const maxReconnectAttempts = 5;
let statusUpdateInterval = null;

// Lucide Icons chargé depuis CDN dans Layout.astro

function ensureProfiles() {
    if (!config.profiles || typeof config.profiles !== 'object') config.profiles = {};
    if (Object.keys(config.profiles).length === 0) config.profiles['Profil 1'] = { keys: {} };
    if (!config.profiles['Configuration']) config.profiles['Configuration'] = { keys: {} };
    if (!config.profiles['Configuration'].keys) config.profiles['Configuration'].keys = {};
    const cfgKeys = config.profiles['Configuration'].keys;
    if (config.profiles['Profil 2']) {
        Object.assign(cfgKeys, config.profiles['Profil 2'].keys || {});
        delete config.profiles['Profil 2'];
        if (config.activeProfile === 'Profil 2') config.activeProfile = 'Configuration';
    }
    const navDefaults = { '1-1': { type: 'key', value: 'UP' }, '2-0': { type: 'key', value: 'Prev' }, '2-1': { type: 'key', value: 'Select' }, '2-2': { type: 'key', value: 'Next' }, '3-1': { type: 'key', value: 'DOWN' } };
    for (const [id, def] of Object.entries(navDefaults)) { if (cfgKeys[id] == null) cfgKeys[id] = def; }
    delete cfgKeys['0-0']; // Toujours supprimer 0-0 car c'est le profile switch
    if (!config.activeProfile || !config.profiles[config.activeProfile]) {
        config.activeProfile = Object.keys(config.profiles)[0] || 'Profil 1';
    }
}

const ARROW_KEYS = []; // Désactivé: les touches row0 sont maintenant configurables normalement
const NAV_DISPLAY_KEYS = ['1-1', '2-0', '2-1', '2-2', '3-1'];

function getCurrentKeys() {
    ensureProfiles();
    const cur = config.profiles[config.activeProfile].keys || {};
    // Les touches row0 (0-1, 0-2, 0-3) sont maintenant configurables normalement dans chaque profil
    // Plus besoin de les fusionner avec le profil Configuration
    return { ...cur };
}

function migrateConfigToProfiles() {
    if (config.keys && typeof config.keys === 'object') {
        if (!config.profiles) config.profiles = {};
        config.profiles['Profil 1'] = { keys: { ...config.keys } };
        config.activeProfile = 'Profil 1';
        delete config.keys;
    }
}

// Initialisation
export function initApp() {
    try {
        setupTabs();
        setupTheme();
        loadConfig();
        migrateConfigToProfiles();
        ensureProfiles();
        setupProfiles();
        initializeGrid();
        setupEventListeners();
        setupBacklightControls();
        setupFingerprintControls();
        setupDisplayControls();
        setupSettingsControls();
        
        updateConnectionStatus(false);
        
        if (typeof lucide !== 'undefined') {
            lucide.createIcons();
        }
        
        startStatusUpdates();
    } catch (error) {
        console.error('[INIT] Error during initialization:', error);
        // Essayer au moins d'afficher la grille même en cas d'erreur
        try {
            initializeGrid();
        } catch (e) {
            console.error('[INIT] Failed to initialize grid:', e);
        }
    }
}

// Configurer le thème
function setupTheme() {
    const themeToggle = document.getElementById('theme-toggle');
    const themeIcon = document.getElementById('theme-icon');
    if (!themeToggle) return;
    
    // Charger le thème sauvegardé ou utiliser 'dark' par défaut
    const savedTheme = localStorage.getItem('theme') || 'dark';
    document.documentElement.setAttribute('data-theme', savedTheme);
    updateThemeIcon(savedTheme, themeIcon);
    
    themeToggle.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        
        const currentTheme = document.documentElement.getAttribute('data-theme');
        const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
        
        document.documentElement.setAttribute('data-theme', newTheme);
        localStorage.setItem('theme', newTheme);
        
        // Mettre à jour l'icône
        updateThemeIcon(newTheme, themeIcon);
        
        // Réinitialiser les icônes Lucide
        if (typeof lucide !== 'undefined') {
            lucide.createIcons();
        }
        
        console.log('Thème changé:', newTheme);
    });
}

// Mettre à jour l'icône du thème
function updateThemeIcon(theme, iconElement) {
    if (!iconElement) return;
    
    // Changer l'icône selon le thème
    iconElement.setAttribute('data-lucide', theme === 'dark' ? 'sun' : 'moon');
    
    // Réinitialiser l'icône
    if (typeof lucide !== 'undefined') {
        lucide.createIcons();
    }
}

// Initialiser la grille
function initializeGrid() {
    console.log('[GRID] Initializing grid...');
    const grid = document.getElementById('key-grid');
    if (!grid) {
        console.warn('[GRID] key-grid element not found, retrying...');
        // Réessayer après un court délai
        setTimeout(() => {
            const retryGrid = document.getElementById('key-grid');
            if (retryGrid) {
                console.log('[GRID] Retrying grid initialization...');
                initializeGrid();
            } else {
                console.error('[GRID] key-grid element still not found after retry');
            }
        }, 100);
        return;
    }
    
    console.log('[GRID] Grid element found, clearing and populating...');
    console.log('[GRID] Config rows:', config.rows, 'cols:', config.cols);
    
    grid.innerHTML = '';
    
    // 5 lignes. 0,0 = changeur de profil (forcé). 0,1–0,3 = configurables normalement dans chaque profil.
    // skip: 2-3 (partie +), 4-1 (partie 4,0), 4-3 (partie =)
    const skipPositions = new Set(['2-3', '4-1', '4-3']);
    
    // S'assurer que config.rows et config.cols sont définis
    if (!config.rows || !config.cols) {
        console.error('[GRID] ERROR: config.rows or config.cols not defined!', config);
        config.rows = 5;
        config.cols = 4;
    }
    
    console.log('[GRID] Initializing grid with', config.rows, 'rows and', config.cols, 'cols');
    
    let keysCreated = 0;
    
    for (let row = 0; row < config.rows; row++) {
        for (let col = 0; col < config.cols; col++) {
            const keyId = `${row}-${col}`;
            if (skipPositions.has(keyId)) continue;
            
            if (keyId === '0-0') {
                const ps = document.createElement('div');
                ps.className = 'key-button profile-switcher-cell';
                ps.id = 'key-0-0';
                ps.dataset.row = '0';
                ps.dataset.col = '0';
                ps.style.gridColumn = '1';
                ps.style.gridRow = '1';
                const lbl = document.createElement('div');
                lbl.className = 'key-label';
                lbl.textContent = 'Profil';
                const val = document.createElement('div');
                val.className = 'key-value';
                val.textContent = config.activeProfile || '—';
                ps.appendChild(lbl);
                ps.appendChild(val);
                ps.addEventListener('click', (e) => {
                    e.stopPropagation(); // Empêcher la propagation
                    switchToNextProfile();
                });
                grid.appendChild(ps);
                keysCreated++;
                continue;
            }
            
            const keyButton = document.createElement('div');
            keyButton.className = 'key-button';
            keyButton.id = `key-${keyId}`;
            keyButton.dataset.row = row;
            keyButton.dataset.col = col;
            
            let gridCol = col + 1, gridRow = row + 1, colSpan = 1, rowSpan = 1;
            
            if (keyId === '4-0') {
                keyButton.classList.add('key-wide');
                keyButton.dataset.spanKeys = '4-0,4-1';
                colSpan = 2;
            } else if (keyId === '1-3') {
                keyButton.classList.add('key-tall');
                keyButton.dataset.spanKeys = '1-3,2-3';
                rowSpan = 2;
            } else if (keyId === '3-3') {
                keyButton.classList.add('key-tall');
                keyButton.dataset.spanKeys = '3-3,4-3';
                rowSpan = 2;
            }
            
            keyButton.style.gridColumn = colSpan > 1 ? `${gridCol} / span ${colSpan}` : String(gridCol);
            keyButton.style.gridRow    = rowSpan > 1 ? `${gridRow} / span ${rowSpan}` : String(gridRow);
            
            const keyLabel = document.createElement('div');
            keyLabel.className = 'key-label';
            keyLabel.textContent = keyId.replace('-', ',');
            
            const keyValue = document.createElement('div');
            keyValue.className = 'key-value';
            
            // Pour les grandes touches, utiliser le keyId principal
            const mainKeyId = keyId;
            const k = getCurrentKeys()[mainKeyId];
            keyValue.textContent = formatKeyLabel(k);
            if (k) keyButton.classList.add('configured');
            
            keyButton.appendChild(keyLabel);
            keyButton.appendChild(keyValue);
            // Empêcher la propagation pour les touches row0 (sauf 0-0 qui est géré séparément)
            keyButton.addEventListener('click', (e) => {
                e.stopPropagation(); // Empêcher la propagation vers d'autres handlers
                selectKey(mainKeyId, row, col);
            });
            
            grid.appendChild(keyButton);
            keysCreated++;
        }
    }
    
    console.log('[GRID] Created', keysCreated, 'keys');
    
    const gs = document.getElementById('grid-size');
    if (gs) gs.textContent = `${config.cols}x${config.rows}`;
    
    if (typeof lucide !== 'undefined') {
        lucide.createIcons();
    }
    
    console.log('[GRID] Grid initialization complete');
    
    // Vérifier que les touches ont bien été ajoutées
    const keysInGrid = grid.querySelectorAll('.key-button');
    console.log('[GRID] Keys in grid:', keysInGrid.length);
    
    if (keysInGrid.length === 0) {
        console.error('[GRID] ERROR: No keys were created!');
    }
}

// Sélectionner une touche
function selectKey(keyId, row, col) {
    // Protection: Ne jamais switcher le profil - le bouton 0-0 a son propre handler
    // Les touches row0 (0-1, 0-2, 0-3) sont maintenant configurables normalement
    if (selectedKey) {
        const el = document.getElementById(`key-${selectedKey}`);
        if (el) {
            // Forcer la mise à jour immédiate en utilisant requestAnimationFrame
            el.classList.remove('selected');
            // Forcer un reflow pour appliquer immédiatement le changement
            void el.offsetWidth;
        }
    }
    selectedKey = keyId;
    const keyButton = document.getElementById(`key-${keyId}`);
    if (keyButton) {
        keyButton.classList.add('selected');
        // Forcer un reflow pour appliquer immédiatement le changement
        void keyButton.offsetWidth;
    }
    
    // Afficher la configuration actuelle
    const keyConfig = getCurrentKeys()[keyId] || {};
    
    // Afficher le label approprié pour les grandes touches
    let displayLabel = keyId.replace('-', ',');
    if (keyId === '4-0') displayLabel = '4,0 (4,0-4,1)';
    else if (keyId === '1-3') displayLabel = '+ (1,3)';
    else if (keyId === '3-3') displayLabel = '= (3,3)';
    else if (config.activeProfile === 'Configuration' && NAV_DISPLAY_KEYS.includes(keyId)) {
        if (keyId === '3-1') displayLabel = '3,1 (↓ nav)';
        else if (keyId === '1-1') displayLabel = '1,1 (↑ nav)';
        else if (keyId === '2-0') displayLabel = '2,0 (← nav)';
        else if (keyId === '2-1') displayLabel = '2,1 (Select nav)';
        else if (keyId === '2-2') displayLabel = '2,2 (→ nav)';
    }
    
    const selectedKeyDisplay = document.getElementById('selected-key-display');
    if (selectedKeyDisplay) selectedKeyDisplay.textContent = displayLabel;
    
    const keyType = document.getElementById('key-type');
    if (keyType) keyType.value = keyConfig.type || 'key';
    
    const keyValue = document.getElementById('key-value');
    if (keyValue) keyValue.value = keyConfig.value || '';
    
    // Afficher/masquer les groupes selon le type
    updateFormVisibility();
    
    // Modificateurs
    if (keyConfig.modifiers) {
        const modCtrl = document.getElementById('mod-ctrl');
        const modShift = document.getElementById('mod-shift');
        const modAlt = document.getElementById('mod-alt');
        const modGui = document.getElementById('mod-gui');
        if (modCtrl) modCtrl.checked = keyConfig.modifiers.includes('CTRL');
        if (modShift) modShift.checked = keyConfig.modifiers.includes('SHIFT');
        if (modAlt) modAlt.checked = keyConfig.modifiers.includes('ALT');
        if (modGui) modGui.checked = keyConfig.modifiers.includes('GUI');
    }
    
    // Macro
    if (keyConfig.macro) {
        const macroSeq = document.getElementById('macro-sequence');
        if (macroSeq) macroSeq.value = keyConfig.macro.join(', ');
    }
}

// Mettre à jour la visibilité des formulaires
function updateFormVisibility() {
    const keyType = document.getElementById('key-type');
    if (!keyType) return;
    const keyTypeValue = keyType.value;
    const keyInputGroup = document.getElementById('key-input-group');
    const mediaPresetGroup = document.getElementById('media-preset-group');
    const modifierGroup = document.getElementById('modifier-group');
    const macroGroup = document.getElementById('macro-group');
    
    if (keyInputGroup) keyInputGroup.style.display = keyTypeValue !== 'macro' ? 'block' : 'none';
    if (mediaPresetGroup) mediaPresetGroup.style.display = keyTypeValue === 'media' ? 'block' : 'none';
    if (modifierGroup) modifierGroup.style.display = (keyTypeValue === 'key' || keyTypeValue === 'modifier') ? 'block' : 'none';
    if (macroGroup) macroGroup.style.display = keyTypeValue === 'macro' ? 'block' : 'none';
    
    const qvg = document.getElementById('quick-values-group');
    const qkr = document.getElementById('quick-keys-row');
    const qar = document.getElementById('quick-arrows-row');
    const qmr = document.getElementById('quick-modifiers-row');
    const qsr = document.getElementById('quick-shortcuts-row');
    if (qvg) qvg.style.display = (keyTypeValue === 'key' || keyTypeValue === 'modifier') ? 'block' : 'none';
    if (qkr) qkr.style.display = keyTypeValue === 'key' ? 'flex' : 'none';
    if (qar) qar.style.display = keyTypeValue === 'key' ? 'flex' : 'none';
    if (qmr) qmr.style.display = keyTypeValue === 'modifier' ? 'flex' : 'none';
    if (qsr) qsr.style.display = keyTypeValue === 'key' ? 'flex' : 'none';
}

// Configurer les écouteurs d'événements
function setupEventListeners() {
    // Type de touche
    const keyType = document.getElementById('key-type');
    if (keyType) keyType.addEventListener('change', updateFormVisibility);
    
    // Capture de touches
    const keyValue = document.getElementById('key-value');
    if (keyValue) {
        keyValue.addEventListener('keydown', (e) => {
            e.preventDefault();
            keyValue.value = getKeyName(e);
            const modCtrl = document.getElementById('mod-ctrl');
            const modShift = document.getElementById('mod-shift');
            const modAlt = document.getElementById('mod-alt');
            const modGui = document.getElementById('mod-gui');
            if (modCtrl) modCtrl.checked = e.ctrlKey;
            if (modShift) modShift.checked = e.shiftKey;
            if (modAlt) modAlt.checked = e.altKey;
            if (modGui) modGui.checked = e.metaKey;
        });
    }
    
    const mediaPreset = document.getElementById('media-preset');
    if (mediaPreset) {
        mediaPreset.addEventListener('change', () => {
            const v = mediaPreset.value;
            if (v && v !== 'custom' && keyValue) keyValue.value = v;
        });
    }
    
    const configPanel = document.querySelector('.config-panel');
    if (configPanel) {
        configPanel.addEventListener('click', (e) => {
            const btn = e.target.closest('.quick-value-btn');
            if (btn && btn.dataset.value) {
                if (keyValue) keyValue.value = btn.dataset.value;
                const mods = (btn.dataset.modifiers || '').split(',').map(s => s.trim()).filter(Boolean);
                const m = (id, k) => { const el = document.getElementById(id); if (el) el.checked = mods.includes(k); };
                m('mod-ctrl', 'CTRL');
                m('mod-shift', 'SHIFT');
                m('mod-alt', 'ALT');
                m('mod-gui', 'GUI');
                if (!btn.dataset.modifiers) {
                    const modCtrl = document.getElementById('mod-ctrl');
                    const modShift = document.getElementById('mod-shift');
                    const modAlt = document.getElementById('mod-alt');
                    const modGui = document.getElementById('mod-gui');
                    if (modCtrl) modCtrl.checked = false;
                    if (modShift) modShift.checked = false;
                    if (modAlt) modAlt.checked = false;
                    if (modGui) modGui.checked = false;
                }
            }
        });
    }
    
    // Boutons
    const saveBtn = document.getElementById('save-btn');
    const clearBtn = document.getElementById('clear-btn');
    const resetBtn = document.getElementById('reset-btn');
    const connectBtn = document.getElementById('connect-btn');
    
    if (saveBtn) saveBtn.addEventListener('click', saveKeyConfig);
    if (clearBtn) clearBtn.addEventListener('click', clearKeyConfig);
    if (resetBtn) resetBtn.addEventListener('click', resetAllKeys);
    if (connectBtn) connectBtn.addEventListener('click', connectToESP32);
}

function formatKeyLabel(k) {
    if (!k) return 'Non configuré';
    if (k.macro) return 'Macro';
    if (k.modifiers && k.modifiers.length && k.value) {
        const names = { CTRL: 'Ctrl', SHIFT: 'Shift', ALT: 'Alt', GUI: 'Win' };
        const m = k.modifiers.map(x => names[x] || x).join('+');
        const v = (k.value.length === 1) ? k.value.toUpperCase() : k.value;
        return m + '+' + v;
    }
    return k.value || '—';
}

// Obtenir le nom de la touche
function getKeyName(event) {
    if (event.key === ' ') return 'SPACE';
    if (event.key === 'Enter') return 'ENTER';
    if (event.key === 'Tab') return 'TAB';
    if (event.key === 'Escape') return 'ESC';
    if (event.key === 'Backspace') return 'BACKSPACE';
    if (event.key === 'Delete') return 'DELETE';
    if (event.key === 'ArrowUp') return 'UP';
    if (event.key === 'ArrowDown') return 'DOWN';
    if (event.key === 'ArrowLeft') return 'LEFT';
    if (event.key === 'ArrowRight') return 'RIGHT';
    if (event.key.length === 1) {
        return event.key.toUpperCase();
    }
    return event.key.toUpperCase();
}

// Enregistrer la configuration d'une touche
function saveKeyConfig() {
    if (!selectedKey) {
        alert('Veuillez sélectionner une touche');
        return;
    }
    
    const keyType = document.getElementById('key-type');
    const keyValue = document.getElementById('key-value');
    if (!keyType || !keyValue) return;
    
    const keyTypeValue = keyType.value;
    const keyValueValue = keyValue.value;
    
    if (!keyValueValue && keyTypeValue !== 'macro') {
        alert('Veuillez entrer une valeur');
        return;
    }
    
    const keyConfig = {
        type: keyTypeValue,
        value: keyValueValue
    };
    
    // Modificateurs
    if (keyTypeValue === 'key' || keyTypeValue === 'modifier') {
        const modifiers = [];
        const modCtrl = document.getElementById('mod-ctrl');
        const modShift = document.getElementById('mod-shift');
        const modAlt = document.getElementById('mod-alt');
        const modGui = document.getElementById('mod-gui');
        if (modCtrl && modCtrl.checked) modifiers.push('CTRL');
        if (modShift && modShift.checked) modifiers.push('SHIFT');
        if (modAlt && modAlt.checked) modifiers.push('ALT');
        if (modGui && modGui.checked) modifiers.push('GUI');
        if (modifiers.length > 0) {
            keyConfig.modifiers = modifiers;
        }
    }
    
    // Macro
    if (keyTypeValue === 'macro') {
        const macroSequence = document.getElementById('macro-sequence');
        if (macroSequence && macroSequence.value) {
            keyConfig.macro = macroSequence.value.split(',').map(s => s.trim());
        }
    }
    
    const p = config.profiles[config.activeProfile];
    if (!p.keys) p.keys = {};
    p.keys[selectedKey] = keyConfig;
    updateKeyDisplay(selectedKey);
    saveConfig();
    updateDisplayInfo();
    if (config.connected) sendConfigToESP32();
}

// Effacer la configuration d'une touche
function clearKeyConfig() {
    if (!selectedKey) return;
    
    const p = config.profiles[config.activeProfile];
    if (p && p.keys) delete p.keys[selectedKey];
    updateKeyDisplay(selectedKey);
    saveConfig();
    const keyValue = document.getElementById('key-value');
    const macroSequence = document.getElementById('macro-sequence');
    const modCtrl = document.getElementById('mod-ctrl');
    const modShift = document.getElementById('mod-shift');
    const modAlt = document.getElementById('mod-alt');
    const modGui = document.getElementById('mod-gui');
    if (keyValue) keyValue.value = '';
    if (macroSequence) macroSequence.value = '';
    if (modCtrl) modCtrl.checked = false;
    if (modShift) modShift.checked = false;
    if (modAlt) modAlt.checked = false;
    if (modGui) modGui.checked = false;
    if (config.connected) sendConfigToESP32();
}

// Réinitialiser toutes les touches
function resetAllKeys() {
    if (confirm('Êtes-vous sûr de vouloir réinitialiser toutes les touches du profil actuel ?')) {
        config.profiles[config.activeProfile].keys = {};
        saveConfig();
        initializeGrid();
        
        if (ws && ws.readyState === WebSocket.OPEN) {
            sendConfigToESP32();
        }
    }
}

// Mettre à jour l'affichage d'une touche
function updateKeyDisplay(keyId) {
    const keyButton = document.getElementById(`key-${keyId}`);
    if (!keyButton) return;
    const keyValue = keyButton.querySelector('.key-value');
    const keyConfig = getCurrentKeys()[keyId];
    
    if (keyConfig) {
        keyButton.classList.add('configured');
        if (keyValue) keyValue.textContent = formatKeyLabel(keyConfig);
    } else {
        keyButton.classList.remove('configured');
        if (keyValue) keyValue.textContent = 'Non configuré';
    }
}

// Se connecter à l'ESP32
async function connectToESP32() {
    const status = document.getElementById('connection-status');
    const btn = document.getElementById('connect-btn');
    const connectionType = document.getElementById('connection-type');
    if (!btn || !connectionType) return;
    
    const connectionTypeValue = connectionType.value;
    
    // Si déjà connecté, déconnecter
    if (config.connected && config.connectionType) {
        await disconnectFromESP32();
        return;
    }
    
    let connected = false;
    
    switch(connectionTypeValue) {
        case 'bluetooth':
            // Connexion Bluetooth via ESP32 (BluetoothSerial)
            try {
                if ('bluetooth' in navigator) {
                    const device = await navigator.bluetooth.requestDevice({
                        filters: [{ namePrefix: 'Macropad' }],
                        optionalServices: [
                            '0000ffe0-0000-1000-8000-00805f9b34fb', // Service série Bluetooth standard
                            '00001101-0000-1000-8000-00805f9b34fb'  // SPP (Serial Port Profile)
                        ]
                    });
                    
                    const server = await device.gatt.connect();
                    config.bluetoothDevice = device;
                    config.bluetoothServer = server;
                    
                    // Obtenir le service série Bluetooth
                    const service = await server.getPrimaryService('0000ffe0-0000-1000-8000-00805f9b34fb');
                    const characteristic = await service.getCharacteristic('0000ffe1-0000-1000-8000-00805f9b34fb');
                    
                    // Écouter les notifications
                    await characteristic.startNotifications();
                    characteristic.addEventListener('characteristicvaluechanged', (event) => {
                        const value = event.target.value;
                        const decoder = new TextDecoder();
                        const text = decoder.decode(value);
                        try {
                            const data = JSON.parse(text);
                            handleESP32Message(data);
                        } catch (e) {
                            console.log('Données Bluetooth reçues:', text);
                        }
                    });
                    
                    config.bluetoothCharacteristic = characteristic;
                    config.connectionType = 'bluetooth';
                    connected = true;
                } else {
                    alert('Bluetooth n\'est pas supporté sur ce navigateur. Utilisez Chrome ou Edge.');
                    return;
                }
            } catch (error) {
                console.error('Erreur connexion Bluetooth:', error);
                if (error.name === 'NotFoundError') {
                    alert('Aucun appareil Bluetooth trouvé. Assurez-vous que l\'ESP32 est allumé et en mode Bluetooth.');
                } else {
                    alert('Erreur lors de la connexion Bluetooth: ' + error.message);
                }
                return;
            }
            break;
            
        case 'usb':
            // Connexion USB directement depuis l'ESP32-S3 (USB Serial natif / UART)
            try {
                if ('serial' in navigator) {
                    // Demander à l'utilisateur de sélectionner le port série (UART/USB)
                    const port = await navigator.serial.requestPort({});
                    
                    // Ouvrir le port avec les paramètres de l'ESP32-S3
                    await port.open({ 
                        baudRate: 115200,
                        dataBits: 8,
                        parity: 'none',
                        stopBits: 1,
                        flowControl: 'none'
                    });
                    
                    config.serialPort = port;
                    config.connectionType = 'usb';
                    connected = true;
                    
                    console.log('Port série ouvert:', port);
                } else {
                    alert('L\'API Web Serial n\'est pas supportée. Utilisez Chrome ou Edge.');
                    return;
                }
            } catch (error) {
                console.error('Erreur connexion USB/UART:', error);
                console.error('Détails de l\'erreur:', error.name, error.message);
                
                if (error.name === 'NotFoundError') {
                    alert('Aucun port série détecté.\n\nVérifications:\n- L\'ESP32-S3 est-il connecté via USB?\n- Le port apparaît-il dans le Gestionnaire de périphériques?\n- Avez-vous sélectionné le bon port dans la liste?');
                } else if (error.name === 'SecurityError') {
                    alert('Erreur de sécurité: Vérifiez que vous utilisez HTTPS ou localhost, et que vous avez autorisé l\'accès au port série.');
                } else if (error.name === 'InvalidStateError') {
                    alert('Le port est déjà ouvert. Fermez d\'autres applications qui utilisent ce port (Arduino IDE, moniteur série, etc.).');
                } else {
                    alert('Erreur lors de la connexion USB/UART:\n\n' + error.name + ': ' + error.message + '\n\nAssurez-vous que:\n- L\'ESP32-S3 est connecté\n- Aucune autre application n\'utilise le port\n- Le pilote USB est installé correctement');
                }
                return;
            }
            break;
            
        case 'wifi':
            // WiFi désactivé pour le moment (sera activé avec dongle 2.4GHz)
            alert('WiFi désactivé pour le moment. Utilisez USB ou Bluetooth.');
            return;
    }
    
    if (connected) {
        config.connected = true;
        updateConnectionStatus(true, connectionTypeValue);
        
        // Mettre à jour le bouton avec icône
        const btnIcon = btn.querySelector('.btn-icon');
        if (btnIcon) {
            btnIcon.setAttribute('data-lucide', 'unlink');
        }
        const btnText = btn.querySelector('span');
        if (btnText) {
            btnText.textContent = 'Déconnecter';
        } else {
            btn.innerHTML = '<i data-lucide="unlink" class="btn-icon"></i><span>Déconnecter</span>';
        }
        
        // Réinitialiser les icônes
        if (typeof createIcons !== 'undefined') {
            createIcons();
        }
        
        // Charger la configuration
        if (connectionTypeValue === 'wifi' || connectionTypeValue === 'usb') {
            // Pour USB/WiFi, on peut lire directement depuis le port série
            startSerialReader();
        }
    }
}

// Mettre à jour l'affichage du statut de connexion
function updateConnectionStatus(isConnected, connectionType = null) {
    const status = document.getElementById('connection-status');
    if (!status) return;
    
    if (isConnected && connectionType) {
        status.textContent = `Connecté (${connectionType.toUpperCase()})`;
        status.className = 'status-value connected';
    } else {
        status.textContent = 'Déconnecté';
        status.className = 'status-value disconnected';
    }
}

// Déconnecter de l'ESP32
async function disconnectFromESP32() {
    const status = document.getElementById('connection-status');
    const btn = document.getElementById('connect-btn');
    
    if (config.serialPort) {
        try {
            // Fermer le port série proprement
            if (config.serialPort.readable) {
                try {
                    const reader = config.serialPort.readable.getReader();
                    await reader.cancel();
                    reader.releaseLock();
                } catch (error) {
                    console.warn('Erreur lors de la fermeture du reader:', error);
                }
            }
            if (config.serialPort.writable) {
                try {
                    const writer = config.serialPort.writable.getWriter();
                    await writer.close();
                    writer.releaseLock();
                } catch (error) {
                    console.warn('Erreur lors de la fermeture du writer:', error);
                }
            }
            // Vérifier que le port a une méthode close avant de l'appeler
            if (config.serialPort && typeof config.serialPort.close === 'function') {
                await config.serialPort.close();
            }
        } catch (error) {
            console.error('Erreur lors de la fermeture du port:', error);
        }
        config.serialPort = null;
    }
    
    if (config.bluetoothDevice) {
        try {
            // Vérifier que gatt existe et est connecté avant de déconnecter
            if (config.bluetoothDevice.gatt && config.bluetoothDevice.gatt.connected) {
                await config.bluetoothDevice.gatt.disconnect();
            }
        } catch (error) {
            console.warn('Erreur lors de la déconnexion Bluetooth:', error);
        }
        config.bluetoothDevice = null;
        config.bluetoothServer = null;
        config.bluetoothCharacteristic = null;
    }
    
    config.connected = false;
    config.connectionType = null;
    updateConnectionStatus(false);
    
    // Mettre à jour le bouton avec icône
    if (btn) {
        const btnIcon = btn.querySelector('.btn-icon');
        if (btnIcon) {
            btnIcon.setAttribute('data-lucide', 'link');
        }
        const btnText = btn.querySelector('span');
        if (btnText) {
            btnText.textContent = 'Se connecter';
        } else {
            btn.innerHTML = '<i data-lucide="link" class="btn-icon"></i><span>Se connecter</span>';
        }
    }
    
    // Réinitialiser les icônes
    if (typeof lucide !== 'undefined') {
        lucide.createIcons();
    }
}

// Lire depuis le port série (USB/WiFi/UART)
async function startSerialReader() {
    if (!config.serialPort) return;
    
    try {
        const reader = config.serialPort.readable.getReader();
        let buffer = '';
        
        while (true) {
            try {
                const { value, done } = await reader.read();
                if (done) {
                    console.log('Port série fermé');
                    break;
                }
                
                // Traiter les données reçues
                const text = new TextDecoder().decode(value);
                buffer += text;
                
                // Traiter les lignes complètes (séparées par \n ou \r\n)
                const lines = buffer.split(/\r?\n/);
                buffer = lines.pop() || ''; // Garder la dernière ligne incomplète
                
                for (const line of lines) {
                    if (line.trim().length > 0) {
                        console.log('[DEBUG] [WEB_UI] Raw data received:', line);
                        
                        // Parser les messages JSON si nécessaire
                        try {
                            const data = JSON.parse(line);
                            handleESP32Message(data);
                        } catch (e) {
                            // Pas du JSON, traiter comme texte simple
                            console.log('[DEBUG] [WEB_UI] Non-JSON message:', line);
                        }
                    }
                }
            } catch (error) {
                console.error('Erreur lecture série:', error);
                if (error.name === 'NetworkError' || error.name === 'InvalidStateError') {
                    console.error('Port série déconnecté');
                    await disconnectFromESP32();
                }
                break;
            }
        }
        
        reader.releaseLock();
    } catch (error) {
        console.error('Erreur initialisation lecteur série:', error);
    }
}

// Envoyer la configuration à l'ESP32
async function sendConfigToESP32() {
    if (!config.connected) return;
    
    const keys = getCurrentKeys();
    delete keys['0-0'];
    const data = JSON.stringify({
        type: 'config',
        rows: config.rows,
        cols: config.cols,
        keys: keys,
        activeProfile: config.activeProfile,
        profiles: config.profiles,
        profileList: Object.keys(config.profiles).join('|'),
        outputMode: config.outputMode
    });
    
    try {
        if (config.connectionType === 'usb' || config.connectionType === 'wifi') {
            // Envoyer via port série
            if (config.serialPort && config.serialPort.writable) {
                const writer = config.serialPort.writable.getWriter();
                await writer.write(new TextEncoder().encode(data + '\n'));
                writer.releaseLock();
            }
        } else if (config.connectionType === 'bluetooth') {
            // Envoyer via Bluetooth
            if (config.bluetoothCharacteristic) {
                const encoder = new TextEncoder();
                const dataArray = encoder.encode(data + '\n');
                await config.bluetoothCharacteristic.writeValue(dataArray);
            } else {
                console.log('Envoi Bluetooth:', data);
            }
        }
    } catch (error) {
        console.error('Erreur de communication:', error);
    }
}

// Gérer les messages de l'ESP32
function handleESP32Message(data) {
    console.log('[DEBUG] [WEB_UI] Received from ESP32:', data);
    
    switch (data.type) {
        case 'keypress':
            console.log(`[DEBUG] [WEB_UI] Key press: row=${data.row}, col=${data.col}`);
            // Afficher visuellement quelle touche a été pressée
            const keyId = `${data.row}-${data.col}`;
            const keyButton = document.getElementById(`key-${keyId}`);
            if (keyButton) {
                keyButton.style.transform = 'scale(0.95)';
                setTimeout(() => {
                    keyButton.style.transform = '';
                }, 100);
            }
            break;
        case 'status':
            console.log('[DEBUG] [WEB_UI] Status ESP32:', data.message);
            break;
        case 'ota_status':
            handleOTAMessage(data);
            break;
        case 'light':
            console.log(`[DEBUG] [WEB_UI] Light level update: ${data.level}`);
            // Mettre à jour l'affichage de la luminosité ambiante
            if (data.level !== undefined) {
                updateLightLevel(data.level);
            }
            break;
        case 'fingerprint':
            handleFingerprintMessage(data);
            break;
        default:
            console.log('[DEBUG] [WEB_UI] Unknown message type:', data.type);
            break;
    }
}

// Mettre à jour l'affichage de la luminosité ambiante
function updateLightLevel(level) {
    const lightBar = document.getElementById('light-level-bar');
    const lightValue = document.getElementById('light-level-value');
    
    if (lightBar) {
        // Convertir 0-1023 en pourcentage pour l'affichage
        const percent = Math.min(100, Math.round((level / 1023) * 100));
        lightBar.style.width = percent + '%';
    }
    
    if (lightValue) {
        // Convertir en lux approximatif (TEMT6000: ~0.625 lux par unité ADC à 5V)
        const lux = Math.round(level * 0.625);
        lightValue.textContent = lux;
    }
}

// Sauvegarder la configuration
function saveConfig() {
    localStorage.setItem('macropadConfig', JSON.stringify(config));
}

// Charger la configuration
function loadConfig() {
    const saved = localStorage.getItem('macropadConfig');
    if (!saved) return;
    
    let savedConfig;
    try {
        savedConfig = JSON.parse(saved);
        config = { ...config, ...savedConfig };
        // Forcer la grille 4x5 (sans rangée 5)
        config.rows = 5;
        config.cols = 4;
    } catch (e) {
        console.warn('loadConfig: JSON invalide', e);
        return;
    }
    
    if (savedConfig.backlight) {
        const be = document.getElementById('backlight-enabled');
        if (be) be.checked = savedConfig.backlight.enabled !== false;
        const b = savedConfig.backlight.brightness ?? 128;
        const pct = Math.round((b / 255) * 100);
        const sl = document.getElementById('backlight-brightness');
        const pv = document.getElementById('brightness-value');
        if (sl) sl.value = pct;
        if (pv) pv.textContent = pct + '%';
        const ab = document.getElementById('auto-brightness');
        if (ab) ab.checked = savedConfig.backlight.autoBrightness === true;
        const eb = document.getElementById('env-brightness');
        if (eb) eb.checked = savedConfig.backlight.envBrightness === true;
    }
    
    if (savedConfig.display) {
        const defCustom = { showProfile: true, showBattery: true, showMode: true, showKeys: true, showBacklight: true, showCustom1: false, showCustom2: false, customLine1: '', customLine2: '' };
        config.display = { mode: 'data', imageData: null, gifFrames: [], ...savedConfig.display };
        config.display.customData = { ...defCustom, ...(savedConfig.display.customData || {}) };
        const d = config.display.brightness ?? 128;
        const pct = Math.round((d / 255) * 100);
        const sl = document.getElementById('display-brightness');
        const pv = document.getElementById('display-brightness-value');
        if (sl) sl.value = pct;
        if (pv) pv.textContent = pct + '%';
    }
    
    if (savedConfig.fingerprint) {
        config.fingerprint = {
            enabled: savedConfig.fingerprint.enabled || false,
            fingerprints: savedConfig.fingerprint.fingerprints || [],
            maxFingerprints: savedConfig.fingerprint.maxFingerprints || 10
        };
        // Mettre à jour l'interface si elle est déjà initialisée
        if (document.getElementById('fingerprint-list')) {
            updateFingerprintList();
            updateFingerprintStats();
        }
    }
    
    if (savedConfig.settings) {
        config.settings = {
            debug: {
                esp32Enabled: savedConfig.settings.debug?.esp32Enabled || false,
                esp32LogLevel: savedConfig.settings.debug?.esp32LogLevel || 'info',
                atmegaEnabled: savedConfig.settings.debug?.atmegaEnabled || false,
                atmegaLogLevel: savedConfig.settings.debug?.atmegaLogLevel || 'info',
                hid: savedConfig.settings.debug?.hid || false,
                i2c: savedConfig.settings.debug?.i2c || false,
                web: savedConfig.settings.debug?.web || false,
                display: savedConfig.settings.debug?.display || false,
                config: savedConfig.settings.debug?.config || false
            },
            logging: {
                esp32Enabled: savedConfig.settings.logging?.esp32Enabled || false,
                atmegaEnabled: savedConfig.settings.logging?.atmegaEnabled || false
            }
        };
    }
}

// Configurer les contrôles de rétro-éclairage
function setupBacklightControls() {
    const brightnessSlider = document.getElementById('backlight-brightness');
    const brightnessValue = document.getElementById('brightness-value');
    if (!brightnessSlider || !brightnessValue) return;
    
    brightnessSlider.addEventListener('input', (e) => {
        const percent = parseInt(e.target.value, 10) || 0;
        brightnessValue.textContent = percent + '%';
        config.backlight.brightness = Math.round((percent / 100) * 255);
    });
    
    const be = document.getElementById('backlight-enabled');
    if (be) be.addEventListener('change', (e) => { config.backlight.enabled = e.target.checked; saveConfig(); });
    const ab = document.getElementById('auto-brightness');
    if (ab) ab.addEventListener('change', (e) => { config.backlight.autoBrightness = e.target.checked; saveConfig(); });
    const eb = document.getElementById('env-brightness');
    if (eb) {
        eb.addEventListener('change', (e) => { 
            config.backlight.envBrightness = e.target.checked; 
            saveConfig(); 
            // Si on active le mode environnement, désactiver auto-brightness
            if (e.target.checked && ab) {
                ab.checked = false;
                config.backlight.autoBrightness = false;
            }
        });
    }
    const abb = document.getElementById('apply-backlight-btn');
    if (abb) abb.addEventListener('click', async () => { await sendBacklightConfig(); alert('Configuration du rétro-éclairage appliquée'); });
}

// Configurer les contrôles du capteur d'empreinte
function setupFingerprintControls() {
    const fe = document.getElementById('fingerprint-enabled');
    if (fe) {
        fe.checked = config.fingerprint.enabled || false;
        fe.addEventListener('change', (e) => { 
            config.fingerprint.enabled = e.target.checked; 
            saveConfig(); 
            sendFingerprintConfig(); 
        });
    }
    
    // Populate profile select
    populateFingerprintProfileSelect();
    
    const eb = document.getElementById('enroll-btn');
    if (eb) eb.addEventListener('click', () => enrollFingerprint());
    
    const deleteAllBtn = document.getElementById('delete-all-btn');
    if (deleteAllBtn) {
        deleteAllBtn.addEventListener('click', () => { 
            if (confirm('Êtes-vous sûr de vouloir supprimer toutes les empreintes enregistrées ?')) {
                deleteAllFingerprints();
            }
        });
    }
    
    const refreshBtn = document.getElementById('refresh-fingerprints-btn');
    if (refreshBtn) {
        refreshBtn.addEventListener('click', () => {
            requestFingerprintList();
        });
    }
    
    // Initialiser la liste des empreintes
    updateFingerprintList();
    updateFingerprintStats();
    
    // Écouter les changements de profil pour mettre à jour le select
    const profileSelect = document.getElementById('profile-select');
    if (profileSelect) {
        profileSelect.addEventListener('change', populateFingerprintProfileSelect);
    }
}

// Populer le select de profil pour les empreintes
function populateFingerprintProfileSelect() {
    const select = document.getElementById('fingerprint-profile-select');
    if (!select) return;
    
    select.innerHTML = '<option value="">Aucun profil associé</option>';
    
    if (config.profiles) {
        Object.keys(config.profiles).forEach(profileName => {
            if (profileName !== 'Configuration') {
                const option = document.createElement('option');
                option.value = profileName;
                option.textContent = profileName;
                select.appendChild(option);
            }
        });
    }
}

// Mettre à jour la liste des empreintes
function updateFingerprintList() {
    const list = document.getElementById('fingerprint-list');
    if (!list) return;
    
    const fingerprints = config.fingerprint.fingerprints || [];
    
    if (fingerprints.length === 0) {
        list.innerHTML = '<div class="empty-state">Aucune empreinte enregistrée</div>';
        return;
    }
    
    list.innerHTML = '';
    
    fingerprints.forEach((fp, index) => {
        const item = document.createElement('div');
        item.className = 'fingerprint-item';
        item.dataset.fingerprintId = fp.id || index;
        
        const name = fp.name || `Empreinte ${fp.id || index + 1}`;
        const profileName = fp.profileId || 'Aucun';
        const enrolledDate = fp.enrolledDate ? new Date(fp.enrolledDate).toLocaleDateString() : 'Date inconnue';
        const lastUsed = fp.lastUsed ? new Date(fp.lastUsed).toLocaleDateString() : 'Jamais';
        
        item.innerHTML = `
            <div class="fingerprint-item-header">
                <div class="fingerprint-item-info">
                    <div class="fingerprint-item-name">${name}</div>
                    <div class="fingerprint-item-meta">
                        <span class="fingerprint-item-id">ID: ${fp.id || index + 1}</span>
                        <span class="fingerprint-item-profile">Profil: ${profileName}</span>
                    </div>
                </div>
                <div class="fingerprint-item-actions">
                    <button class="btn-icon-small" data-action="rename" data-fp-id="${fp.id || index}" title="Renommer">
                        <i data-lucide="edit-2"></i>
                    </button>
                    <button class="btn-icon-small btn-danger" data-action="delete" data-fp-id="${fp.id || index}" title="Supprimer">
                        <i data-lucide="trash-2"></i>
                    </button>
                </div>
            </div>
            <div class="fingerprint-item-details">
                <div class="fingerprint-item-detail">
                    <span class="detail-label">Enregistrée:</span>
                    <span class="detail-value">${enrolledDate}</span>
                </div>
                <div class="fingerprint-item-detail">
                    <span class="detail-label">Dernière utilisation:</span>
                    <span class="detail-value">${lastUsed}</span>
                </div>
            </div>
        `;
        
        list.appendChild(item);
    });
    
    // Ajouter les event listeners
    list.querySelectorAll('[data-action="rename"]').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const fpId = parseInt(btn.dataset.fpId);
            renameFingerprint(fpId);
        });
    });
    
    list.querySelectorAll('[data-action="delete"]').forEach(btn => {
        btn.addEventListener('click', (e) => {
            const fpId = parseInt(btn.dataset.fpId);
            if (confirm(`Supprimer l'empreinte "${fingerprints.find(f => f.id === fpId)?.name || fpId}" ?`)) {
                deleteFingerprint(fpId);
            }
        });
    });
    
    // Réinitialiser les icônes Lucide
    if (typeof lucide !== 'undefined') {
        lucide.createIcons();
    }
}

// Mettre à jour les statistiques des empreintes
function updateFingerprintStats() {
    const count = document.getElementById('fingerprint-count');
    const capacity = document.getElementById('fingerprint-capacity');
    
    const fingerprints = config.fingerprint.fingerprints || [];
    const maxFingerprints = config.fingerprint.maxFingerprints || 10;
    
    if (count) count.textContent = fingerprints.length;
    if (capacity) capacity.textContent = `${fingerprints.length}/${maxFingerprints}`;
}

// Renommer une empreinte
function renameFingerprint(fpId) {
    const fingerprints = config.fingerprint.fingerprints || [];
    const fp = fingerprints.find(f => f.id === fpId);
    if (!fp) return;
    
    const newName = prompt(`Renommer l'empreinte "${fp.name || `Empreinte ${fpId}`}":`, fp.name || `Empreinte ${fpId}`);
    if (newName && newName.trim()) {
        fp.name = newName.trim();
        saveConfig();
        updateFingerprintList();
        sendFingerprintUpdate(fp);
    }
}

// Supprimer une empreinte spécifique
async function deleteFingerprint(fpId) {
    if (!config.connected) {
        alert('Veuillez d\'abord vous connecter');
        return;
    }
    
    const data = JSON.stringify({
        type: 'fingerprint',
        action: 'delete',
        fingerprintId: fpId
    });
    
    await sendDataToESP32(data);
    
    // Retirer de la liste locale
    config.fingerprint.fingerprints = (config.fingerprint.fingerprints || []).filter(f => f.id !== fpId);
    saveConfig();
    updateFingerprintList();
    updateFingerprintStats();
    
    const feedback = document.getElementById('fingerprint-feedback');
    if (feedback) {
        feedback.className = 'feedback-message success';
        feedback.textContent = 'Empreinte supprimée';
        feedback.style.display = 'block';
        setTimeout(() => { feedback.style.display = 'none'; }, 3000);
    }
}

// Demander la liste des empreintes depuis l'ESP32
async function requestFingerprintList() {
    if (!config.connected) return;
    
    const data = JSON.stringify({
        type: 'fingerprint',
        action: 'list'
    });
    
    await sendDataToESP32(data);
}

// --- Conversion image → 1-bit (128×64) pour écran OLED ---
const DISPLAY_W = 128, DISPLAY_H = 64;

function canvasTo1BitBase64(canvas) {
    const ctx = canvas.getContext('2d');
    const id = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const w = id.width, h = id.height;
    const buf = new Uint8Array(Math.ceil((w * h) / 8));
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const i = (y * w + x) * 4;
            const lum = 0.299 * id.data[i] + 0.587 * id.data[i + 1] + 0.114 * id.data[i + 2];
            const bit = lum >= 128 ? 1 : 0;
            const bi = y * Math.ceil(w / 8) + (x >> 3);
            buf[bi] |= (bit << (7 - (x & 7)));
        }
    }
    let s = '';
    for (let i = 0; i < buf.length; i++) s += String.fromCharCode(buf[i]);
    return btoa(s);
}

function draw1BitToCanvas(base64, canvas) {
    if (!base64 || !canvas) return;
    try {
        const bin = atob(base64);
        const buf = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
        const w = DISPLAY_W, h = DISPLAY_H;
        const ctx = canvas.getContext('2d');
        const id = ctx.createImageData(w, h);
        for (let y = 0; y < h; y++) {
            for (let x = 0; x < w; x++) {
                const bi = y * (w >> 3) + (x >> 3);
                const bit = (buf[bi] >> (7 - (x & 7))) & 1;
                const c = bit ? 255 : 0;
                const i = (y * w + x) * 4;
                id.data[i] = id.data[i + 1] = id.data[i + 2] = c;
                id.data[i + 3] = 255;
            }
        }
        ctx.putImageData(id, 0, 0);
    } catch (e) { console.warn('draw1BitToCanvas:', e); }
}

function drawImageCover(img, canvas, w, h) {
    const cw = canvas.width = w;
    const ch = canvas.height = h;
    const ia = img.width / img.height, ta = w / h;
    let sx = 0, sy = 0, sw = img.width, sh = img.height;
    if (ia > ta) {
        sw = img.height * ta;
        sx = (img.width - sw) / 2;
    } else {
        sh = img.width / ta;
        sy = (img.height - sh) / 2;
    }
    canvas.getContext('2d').drawImage(img, sx, sy, sw, sh, 0, 0, cw, ch);
}

function imageFileTo1BitBase64(file) {
    return new Promise((resolve, reject) => {
        const img = new Image();
        const url = URL.createObjectURL(file);
        img.onload = () => {
            URL.revokeObjectURL(url);
            const c = document.createElement('canvas');
            drawImageCover(img, c, DISPLAY_W, DISPLAY_H);
            resolve(canvasTo1BitBase64(c));
        };
        img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Erreur chargement image')); };
        img.src = url;
    });
}

async function gifFileToFrames(file) {
    try {
        const { parseGIF, decompressFrames } = await import('https://cdn.jsdelivr.net/npm/gifuct-js@2.1.2/+esm');
        const ab = await file.arrayBuffer();
        const gif = parseGIF(ab);
        const frames = decompressFrames(gif, true);
        const w = gif.lsd.width, h = gif.lsd.height;
        const full = new Uint8ClampedArray(w * h * 4);
        const out = [];
        const outCanvas = document.createElement('canvas');
        outCanvas.width = DISPLAY_W;
        outCanvas.height = DISPLAY_H;
        const outCtx = outCanvas.getContext('2d');
        const tmp = document.createElement('canvas');
        tmp.width = w;
        tmp.height = h;
        const tmpCtx = tmp.getContext('2d');
        for (let i = 0; i < frames.length; i++) {
            const f = frames[i];
            if (i > 0 && f.disposalType === 2) {
                const d = frames[i - 1].dims;
                for (let y = d.top; y < d.top + d.height; y++) {
                    for (let x = d.left; x < d.left + d.width; x++) {
                        const idx = (y * w + x) * 4;
                        full[idx] = full[idx + 1] = full[idx + 2] = 0;
                        full[idx + 3] = 255;
                    }
                }
            }
            const d = f.dims;
            for (let py = 0; py < d.height; py++) {
                for (let px = 0; px < d.width; px++) {
                    const src = (py * d.width + px) * 4;
                    const dy = d.top + py, dx = d.left + px;
                    const dst = (dy * w + dx) * 4;
                    full[dst] = f.patch[src];
                    full[dst + 1] = f.patch[src + 1];
                    full[dst + 2] = f.patch[src + 2];
                    full[dst + 3] = f.patch[src + 3];
                }
            }
            const id = new ImageData(full, w, h);
            tmpCtx.putImageData(id, 0, 0);
            outCtx.clearRect(0, 0, DISPLAY_W, DISPLAY_H);
            outCtx.drawImage(tmp, 0, 0, w, h, 0, 0, DISPLAY_W, DISPLAY_H);
            out.push(canvasTo1BitBase64(outCanvas));
        }
        return { frames: out, delays: frames.map(f => (f.delay || 10) * 10) };
    } catch (e) {
        console.warn('gifFileToFrames:', e);
        const b64 = await imageFileTo1BitBase64(file);
        return { frames: [b64], delays: [100] };
    }
}

function setupDisplayImageUpload() {
    const input = document.getElementById('display-image-input');
    const preview = document.getElementById('display-image-preview');
    const info = document.getElementById('display-image-info');
    const remove = document.getElementById('display-image-remove');
    const modeSel = document.getElementById('display-screen-mode');
    if (!input || !preview) return;

    if (config.display.mode && modeSel) modeSel.value = config.display.mode;
    if (config.display.imageData) {
        draw1BitToCanvas(config.display.imageData, preview);
        if (info) info.textContent = 'Image statique (sauvegardée)';
    }
    if (config.display.gifFrames && config.display.gifFrames.length) {
        draw1BitToCanvas(config.display.gifFrames[0], preview);
        if (info) info.textContent = `GIF: ${config.display.gifFrames.length} image(s)`;
    }

    input.addEventListener('change', async (e) => {
        const f = e.target.files[0];
        if (!f) return;
        const isGif = /\.gif$/i.test(f.name) || (f.type === 'image/gif');
        try {
            if (isGif) {
                const { frames, delays } = await gifFileToFrames(f);
                config.display.gifFrames = frames;
                config.display.gifDelays = delays;
                config.display.imageData = null;
                config.display.mode = frames.length > 1 ? 'gif' : 'image';
                if (frames.length > 1) config.display.imageData = null;
                else config.display.imageData = frames[0];
                draw1BitToCanvas(frames[0], preview);
                if (info) info.textContent = frames.length > 1 ? `GIF: ${frames.length} image(s)` : 'Image (1 frame)';
            } else {
                const b64 = await imageFileTo1BitBase64(f);
                config.display.imageData = b64;
                config.display.gifFrames = [];
                config.display.mode = 'image';
                draw1BitToCanvas(b64, preview);
                if (info) info.textContent = 'Image statique';
            }
            if (modeSel) modeSel.value = config.display.mode;
        } catch (err) {
            if (info) info.textContent = 'Erreur: ' + (err.message || 'inconnu');
        }
        saveConfig();
    });

    if (remove) remove.addEventListener('click', () => {
        config.display.imageData = null;
        config.display.gifFrames = [];
        config.display.gifDelays = [];
        config.display.mode = 'data';
        if (modeSel) modeSel.value = 'data';
        preview.getContext('2d').clearRect(0, 0, preview.width, preview.height);
        if (info) info.textContent = '';
        input.value = '';
        saveConfig();
    });

    if (modeSel) modeSel.addEventListener('change', () => {
        config.display.mode = modeSel.value;
        saveConfig();
    });
}

function setupDisplayCustomData() {
    const cd = config.display.customData || {};
    const ids = { 'display-show-profile': 'showProfile', 'display-show-battery': 'showBattery', 'display-show-mode': 'showMode', 'display-show-keys': 'showKeys', 'display-show-backlight': 'showBacklight', 'display-show-custom1': 'showCustom1', 'display-show-custom2': 'showCustom2' };
    for (const [id, key] of Object.entries(ids)) {
        const el = document.getElementById(id);
        if (el) {
            if (cd[key] !== undefined) el.checked = !!cd[key];
            el.addEventListener('change', () => {
                if (!config.display.customData) config.display.customData = {};
                config.display.customData[key] = el.checked;
                saveConfig();
            });
        }
    }
    const l1 = document.getElementById('display-custom-line1');
    const l2 = document.getElementById('display-custom-line2');
    if (l1) { if (cd.customLine1 != null) l1.value = cd.customLine1; l1.addEventListener('input', () => { if (!config.display.customData) config.display.customData = {}; config.display.customData.customLine1 = l1.value; saveConfig(); }); }
    if (l2) { if (cd.customLine2 != null) l2.value = cd.customLine2; l2.addEventListener('input', () => { if (!config.display.customData) config.display.customData = {}; config.display.customData.customLine2 = l2.value; saveConfig(); }); }
}

// Configurer les contrôles de l'écran
// Configurer les contrôles des paramètres
function setupSettingsControls() {
    // Vérifier que les éléments existent (l'onglet Paramètres pourrait ne pas être chargé)
    const settingsPanel = document.getElementById('tab-settings');
    if (!settingsPanel) {
        // L'onglet Paramètres n'existe pas encore, on ne fait rien
        return;
    }
    
    // Charger les paramètres sauvegardés
    loadSettings();
    
    // Debug ESP32
    const debugEsp32 = document.getElementById('debug-esp32-enabled');
    if (debugEsp32) {
        debugEsp32.checked = config.settings?.debug?.esp32Enabled || false;
        debugEsp32.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.esp32Enabled = e.target.checked;
            saveConfig();
        });
    }
    
    // Logging ESP32
    const loggingEsp32 = document.getElementById('logging-esp32-enabled');
    if (loggingEsp32) {
        loggingEsp32.checked = config.settings?.logging?.esp32Enabled || false;
        loggingEsp32.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.logging) config.settings.logging = {};
            config.settings.logging.esp32Enabled = e.target.checked;
            saveConfig();
        });
    }
    
    // Logging ATmega
    const loggingAtmega = document.getElementById('logging-atmega-enabled');
    if (loggingAtmega) {
        loggingAtmega.checked = config.settings?.logging?.atmegaEnabled || false;
        loggingAtmega.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.logging) config.settings.logging = {};
            config.settings.logging.atmegaEnabled = e.target.checked;
            saveConfig();
        });
    }
    
    // Niveaux de log
    const esp32LogLevel = document.getElementById('esp32-log-level');
    if (esp32LogLevel) {
        esp32LogLevel.value = config.settings?.debug?.esp32LogLevel || 'info';
        esp32LogLevel.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.esp32LogLevel = e.target.value;
            saveConfig();
        });
    }
    
    const atmegaLogLevel = document.getElementById('atmega-log-level');
    if (atmegaLogLevel) {
        atmegaLogLevel.value = config.settings?.debug?.atmegaLogLevel || 'info';
        atmegaLogLevel.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.atmegaLogLevel = e.target.value;
            saveConfig();
        });
    }
    
    // Options de debug spécifiques
    const debugHid = document.getElementById('debug-hid-enabled');
    if (debugHid) {
        debugHid.checked = config.settings?.debug?.hid || false;
        debugHid.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.hid = e.target.checked;
            saveConfig();
        });
    }
    
    const debugI2c = document.getElementById('debug-i2c-enabled');
    if (debugI2c) {
        debugI2c.checked = config.settings?.debug?.i2c || false;
        debugI2c.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.i2c = e.target.checked;
            saveConfig();
        });
    }
    
    const debugWeb = document.getElementById('debug-web-enabled');
    if (debugWeb) {
        debugWeb.checked = config.settings?.debug?.web || false;
        debugWeb.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.web = e.target.checked;
            saveConfig();
        });
    }
    
    const debugDisplay = document.getElementById('debug-display-enabled');
    if (debugDisplay) {
        debugDisplay.checked = config.settings?.debug?.display || false;
        debugDisplay.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.display = e.target.checked;
            saveConfig();
        });
    }
    
    const debugConfig = document.getElementById('debug-config-enabled');
    if (debugConfig) {
        debugConfig.checked = config.settings?.debug?.config || false;
        debugConfig.addEventListener('change', (e) => {
            if (!config.settings) config.settings = { debug: {}, logging: {} };
            if (!config.settings.debug) config.settings.debug = {};
            config.settings.debug.config = e.target.checked;
            saveConfig();
        });
    }
    
    // Bouton Appliquer
    const applyBtn = document.getElementById('apply-settings-btn');
    if (applyBtn) {
        applyBtn.addEventListener('click', async () => {
            await sendSettingsToESP32();
            const feedback = document.getElementById('settings-feedback');
            if (feedback) {
                feedback.className = 'feedback-message success';
                feedback.textContent = 'Paramètres appliqués avec succès';
                feedback.style.display = 'block';
                setTimeout(() => { feedback.style.display = 'none'; }, 3000);
            }
        });
    }
    
    // Bouton Réinitialiser
    const resetBtn = document.getElementById('reset-settings-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', () => {
            if (confirm('Réinitialiser tous les paramètres de debug et logging ?')) {
                resetSettings();
            }
        });
    }
    
    // OTA Update
    const otaUpdateBtn = document.getElementById('ota-update-btn');
    const otaFileInput = document.getElementById('ota-file-input');
    const otaProgress = document.getElementById('ota-progress');
    const otaProgressBar = document.getElementById('ota-progress-bar');
    const otaProgressText = document.getElementById('ota-progress-text');
    
    if (otaUpdateBtn && otaFileInput) {
        otaUpdateBtn.addEventListener('click', async () => {
            const file = otaFileInput.files[0];
            if (!file) {
                alert('Veuillez sélectionner un fichier .py');
                return;
            }
            
            if (!file.name.endsWith('.py')) {
                alert('Le fichier doit être un fichier .py');
                return;
            }
            
            if (!confirm(`Mettre à jour le firmware avec ${file.name} ?\n\nL\'ESP32 va redémarrer après la mise à jour.`)) {
                return;
            }
            
            await performOTAUpdate(file);
        });
    }
}

// Fonction pour effectuer une mise à jour OTA
async function performOTAUpdate(file) {
    const otaProgress = document.getElementById('ota-progress');
    const otaProgressBar = document.getElementById('ota-progress-bar');
    const otaProgressText = document.getElementById('ota-progress-text');
    const otaUpdateBtn = document.getElementById('ota-update-btn');
    
    try {
        // Lire le fichier
        const fileContent = await file.text();
        const fileSize = fileContent.length;
        
        // Calculer le nombre de chunks (max 512 bytes par chunk pour BLE GATT)
        // Base64 augmente la taille de ~33%, et le JSON wrapper prend ~50-80 bytes
        // Donc: (512 - 80) / 1.33 ≈ 325 bytes max pour les données originales
        // On utilise 200 caractères pour être sûr de rester sous 512 bytes
        const chunkSize = 200; // Taille de chunk en caractères (après base64 + JSON < 512 bytes)
        const totalChunks = Math.ceil(fileContent.length / chunkSize);
        
        // Afficher la progression
        otaProgress.style.display = 'block';
        otaProgressBar.style.width = '0%';
        otaProgressText.textContent = '0%';
        otaUpdateBtn.disabled = true;
        
        // Envoyer le message de début
        const startMessage = {
            type: 'ota_start',
            filename: file.name,
            size: fileSize,
            chunks: totalChunks
        };
        await sendDataToESP32(JSON.stringify(startMessage));
        
        // Attendre un peu pour que l'ESP32 soit prêt
        await new Promise(resolve => setTimeout(resolve, 500));
        
        // Envoyer les chunks
        for (let i = 0; i < totalChunks; i++) {
            const start = i * chunkSize;
            const end = Math.min(start + chunkSize, fileContent.length);
            const chunk = fileContent.substring(start, end);
            
            // Encoder en base64 pour éviter les problèmes de caractères spéciaux
            const chunkBase64 = btoa(unescape(encodeURIComponent(chunk)));
            
            const chunkMessage = {
                type: 'ota_chunk',
                index: i,
                data: chunkBase64,
                encoded: true
            };
            
            // Vérifier la taille du message avant envoi (limite BLE GATT = 512 bytes)
            const messageStr = JSON.stringify(chunkMessage);
            const messageSize = new TextEncoder().encode(messageStr).length;
            
            if (messageSize > 512) {
                console.error(`[OTA] Chunk ${i} too large: ${messageSize} bytes (max 512)`);
                throw new Error(`Chunk ${i} is too large (${messageSize} bytes). Reduce chunk size.`);
            }
            
            await sendDataToESP32(messageStr);
            
            // Mettre à jour la progression
            const progress = Math.round(((i + 1) / totalChunks) * 100);
            otaProgressBar.style.width = progress + '%';
            otaProgressText.textContent = `${progress}% (${i + 1}/${totalChunks})`;
            
            // Petit délai entre les chunks pour éviter de surcharger BLE
            await new Promise(resolve => setTimeout(resolve, 100));
        }
        
        // Envoyer le message de fin
        const endMessage = {
            type: 'ota_end'
        };
        await sendDataToESP32(JSON.stringify(endMessage));
        
        // Attendre la confirmation
        otaProgressText.textContent = 'Mise à jour terminée, redémarrage...';
        otaProgressBar.style.width = '100%';
        
        // Réinitialiser après 3 secondes
        setTimeout(() => {
            otaProgress.style.display = 'none';
            otaUpdateBtn.disabled = false;
            const otaFileInput = document.getElementById('ota-file-input');
            if (otaFileInput) {
                otaFileInput.value = '';
            }
        }, 3000);
        
    } catch (error) {
        console.error('OTA Update Error:', error);
        alert('Erreur lors de la mise à jour: ' + error.message);
        otaProgress.style.display = 'none';
        if (otaUpdateBtn) {
            otaUpdateBtn.disabled = false;
        }
    }
}

// Gérer les messages OTA de l'ESP32
function handleOTAMessage(data) {
    const otaProgress = document.getElementById('ota-progress');
    const otaProgressBar = document.getElementById('ota-progress-bar');
    const otaProgressText = document.getElementById('ota-progress-text');
    
    switch (data.status) {
        case 'started':
            console.log('[OTA] Update started');
            if (otaProgress) otaProgress.style.display = 'block';
            break;
        case 'progress':
            if (otaProgressBar) {
                otaProgressBar.style.width = data.progress + '%';
            }
            if (otaProgressText) {
                otaProgressText.textContent = `${data.progress}% (${data.chunk}/${data.total})`;
            }
            break;
        case 'completed':
            console.log('[OTA] Update completed');
            if (otaProgressText) {
                otaProgressText.textContent = 'Mise à jour terminée, redémarrage...';
            }
            if (otaProgressBar) {
                otaProgressBar.style.width = '100%';
            }
            break;
        default:
            console.log('[OTA] Unknown status:', data.status);
    }
}

// Charger les paramètres depuis localStorage
function loadSettings() {
    // Les paramètres sont déjà chargés avec loadConfig()
    // On met juste à jour l'interface si les éléments existent
    try {
        const debugEsp32 = document.getElementById('debug-esp32-enabled');
        const loggingEsp32 = document.getElementById('logging-esp32-enabled');
        const loggingAtmega = document.getElementById('logging-atmega-enabled');
        const esp32LogLevel = document.getElementById('esp32-log-level');
        const atmegaLogLevel = document.getElementById('atmega-log-level');
        const debugHid = document.getElementById('debug-hid-enabled');
        const debugI2c = document.getElementById('debug-i2c-enabled');
        const debugWeb = document.getElementById('debug-web-enabled');
        const debugDisplay = document.getElementById('debug-display-enabled');
        const debugConfig = document.getElementById('debug-config-enabled');
        
        if (debugEsp32) debugEsp32.checked = config.settings?.debug?.esp32Enabled || false;
        if (loggingEsp32) loggingEsp32.checked = config.settings?.logging?.esp32Enabled || false;
        if (loggingAtmega) loggingAtmega.checked = config.settings?.logging?.atmegaEnabled || false;
        if (esp32LogLevel) esp32LogLevel.value = config.settings?.debug?.esp32LogLevel || 'info';
        if (atmegaLogLevel) atmegaLogLevel.value = config.settings?.debug?.atmegaLogLevel || 'info';
        if (debugHid) debugHid.checked = config.settings?.debug?.hid || false;
        if (debugI2c) debugI2c.checked = config.settings?.debug?.i2c || false;
        if (debugWeb) debugWeb.checked = config.settings?.debug?.web || false;
        if (debugDisplay) debugDisplay.checked = config.settings?.debug?.display || false;
        if (debugConfig) debugConfig.checked = config.settings?.debug?.config || false;
    } catch (error) {
        console.warn('[SETTINGS] Error loading settings:', error);
    }
}

// Réinitialiser les paramètres
function resetSettings() {
    config.settings = {
        debug: {
            esp32Enabled: false,
            esp32LogLevel: 'info',
            atmegaEnabled: false,
            atmegaLogLevel: 'info',
            hid: false,
            i2c: false,
            web: false,
            display: false,
            config: false
        },
        logging: {
            esp32Enabled: false,
            atmegaEnabled: false
        }
    };
    saveConfig();
    loadSettings();
    sendSettingsToESP32();
}

// Envoyer les paramètres à l'ESP32
async function sendSettingsToESP32() {
    if (!config.connected) {
        alert('Veuillez d\'abord vous connecter');
        return;
    }
    
    const data = JSON.stringify({
        type: 'settings',
        debug: {
            esp32Enabled: config.settings?.debug?.esp32Enabled || false,
            esp32LogLevel: config.settings?.debug?.esp32LogLevel || 'info',
            atmegaEnabled: config.settings?.debug?.atmegaEnabled || false,
            atmegaLogLevel: config.settings?.debug?.atmegaLogLevel || 'info',
            hid: config.settings?.debug?.hid || false,
            i2c: config.settings?.debug?.i2c || false,
            web: config.settings?.debug?.web || false,
            display: config.settings?.debug?.display || false,
            config: config.settings?.debug?.config || false
        },
        logging: {
            esp32Enabled: config.settings?.logging?.esp32Enabled || false,
            atmegaEnabled: config.settings?.logging?.atmegaEnabled || false
        }
    });
    
    await sendDataToESP32(data);
}

// Gérer les messages de paramètres de l'ESP32
function handleSettingsMessage(data) {
    const feedback = document.getElementById('settings-feedback');
    
    if (data.status === 'success') {
        if (feedback) {
            feedback.className = 'feedback-message success';
            feedback.textContent = 'Paramètres appliqués avec succès';
            feedback.style.display = 'block';
            setTimeout(() => { feedback.style.display = 'none'; }, 3000);
        }
    } else if (data.status === 'error') {
        if (feedback) {
            feedback.className = 'feedback-message error';
            feedback.textContent = 'Erreur lors de l\'application des paramètres: ' + (data.message || 'Erreur inconnue');
            feedback.style.display = 'block';
            setTimeout(() => { feedback.style.display = 'none'; }, 5000);
        }
    }
}

function setupDisplayControls() {
    const displayBrightness = document.getElementById('display-brightness');
    const displayBrightnessValue = document.getElementById('display-brightness-value');
    if (displayBrightness && displayBrightnessValue) {
        displayBrightness.addEventListener('input', (e) => {
            const percent = parseInt(e.target.value, 10) || 0;
            displayBrightnessValue.textContent = percent + '%';
            config.display.brightness = Math.round((percent / 100) * 255);
        });
    }

    setupDisplayImageUpload();
    setupDisplayCustomData();

    const adb = document.getElementById('apply-display-btn');
    if (adb) adb.addEventListener('click', async () => { await sendDisplayConfig(); alert('Configuration de l\'écran appliquée'); });
}

// Envoyer la configuration du rétro-éclairage
async function sendBacklightConfig() {
    if (!config.connected) {
        console.warn('[DEBUG] Cannot send backlight config - not connected');
        return;
    }
    
    const data = JSON.stringify({
        type: 'backlight',
        ...config.backlight
    });
    
    console.log('[DEBUG] Sending backlight config:', data);
    await sendDataToESP32(data);
}

// Envoyer la configuration du capteur d'empreinte
async function sendFingerprintConfig() {
    if (!config.connected) return;
    
    const data = JSON.stringify({
        type: 'fingerprint',
        enabled: config.fingerprint.enabled
    });
    
    await sendDataToESP32(data);
}

// Enregistrer une empreinte
async function enrollFingerprint() {
    if (!config.connected) {
        alert('Veuillez d\'abord vous connecter');
        return;
    }
    
    const nameInput = document.getElementById('fingerprint-name-input');
    const profileSelect = document.getElementById('fingerprint-profile-select');
    
    const name = nameInput?.value.trim() || '';
    const profileId = profileSelect?.value || '';
    
    if (!name) {
        alert('Veuillez entrer un nom pour l\'empreinte');
        nameInput?.focus();
        return;
    }
    
    // Vérifier la capacité
    const fingerprints = config.fingerprint.fingerprints || [];
    if (fingerprints.length >= (config.fingerprint.maxFingerprints || 10)) {
        alert(`Limite atteinte (${config.fingerprint.maxFingerprints || 10} empreintes maximum)`);
        return;
    }
    
    const feedback = document.getElementById('fingerprint-feedback');
    if (feedback) {
        feedback.className = 'feedback-message info';
        feedback.textContent = 'Placez votre doigt sur le capteur...';
        feedback.style.display = 'block';
    }
    
    // Trouver le prochain ID disponible
    const nextId = fingerprints.length > 0 ? Math.max(...fingerprints.map(f => f.id || 0)) + 1 : 1;
    
    const data = JSON.stringify({
        type: 'fingerprint',
        action: 'enroll',
        name: name,
        profileId: profileId,
        fingerprintId: nextId
    });
    
    await sendDataToESP32(data);
    
    // Ajouter à la liste locale immédiatement (sera confirmé par l'ESP32)
    const newFingerprint = {
        id: nextId,
        name: name,
        profileId: profileId,
        enrolledDate: new Date().toISOString(),
        lastUsed: null
    };
    
    if (!config.fingerprint.fingerprints) {
        config.fingerprint.fingerprints = [];
    }
    config.fingerprint.fingerprints.push(newFingerprint);
    saveConfig();
    updateFingerprintList();
    updateFingerprintStats();
    
    // Réinitialiser le formulaire
    if (nameInput) nameInput.value = '';
    if (profileSelect) profileSelect.value = '';
    
    // La réponse sera reçue via handleESP32Message
    setTimeout(() => {
        if (feedback) {
            feedback.className = 'feedback-message success';
            feedback.textContent = `Empreinte "${name}" enregistrée avec succès !`;
        }
        updateFingerprintStatus('Prêt');
    }, 3000);
}

// Supprimer toutes les empreintes
async function deleteAllFingerprints() {
    if (!config.connected) {
        alert('Veuillez d\'abord vous connecter');
        return;
    }
    
    const feedback = document.getElementById('fingerprint-feedback');
    
    const data = JSON.stringify({
        type: 'fingerprint',
        action: 'delete_all'
    });
    
    await sendDataToESP32(data);
    
    // Vider la liste locale
    config.fingerprint.fingerprints = [];
    saveConfig();
    updateFingerprintList();
    updateFingerprintStats();
    
    if (feedback) {
        feedback.className = 'feedback-message success';
        feedback.textContent = 'Toutes les empreintes ont été supprimées';
        feedback.style.display = 'block';
        setTimeout(() => { feedback.style.display = 'none'; }, 3000);
    }
    updateFingerprintStatus('Aucune empreinte');
}

// Envoyer une mise à jour d'empreinte
async function sendFingerprintUpdate(fingerprint) {
    if (!config.connected) return;
    
    const data = JSON.stringify({
        type: 'fingerprint',
        action: 'update',
        fingerprint: fingerprint
    });
    
    await sendDataToESP32(data);
}

// Mettre à jour le statut du capteur
function updateFingerprintStatus(status) {
    const statusEl = document.getElementById('fingerprint-status');
    if (statusEl) statusEl.textContent = status;
}

// Gérer les messages fingerprint de l'ESP32
function handleFingerprintMessage(data) {
    const feedback = document.getElementById('fingerprint-feedback');
    
    switch (data.action) {
        case 'enroll_success':
            if (feedback) {
                feedback.className = 'feedback-message success';
                feedback.textContent = `Empreinte "${data.name || 'enregistrée'}" enregistrée avec succès !`;
                feedback.style.display = 'block';
                setTimeout(() => { feedback.style.display = 'none'; }, 5000);
            }
            updateFingerprintStatus('Prêt');
            // Actualiser la liste si nécessaire
            if (data.fingerprint) {
                const fp = config.fingerprint.fingerprints.find(f => f.id === data.fingerprint.id);
                if (fp && data.fingerprint) {
                    Object.assign(fp, data.fingerprint);
                    saveConfig();
                    updateFingerprintList();
                }
            }
            break;
        case 'enroll_error':
            if (feedback) {
                feedback.className = 'feedback-message error';
                feedback.textContent = `Erreur lors de l'enregistrement: ${data.message || 'Erreur inconnue'}`;
                feedback.style.display = 'block';
                setTimeout(() => { feedback.style.display = 'none'; }, 5000);
            }
            // Retirer de la liste locale si l'enregistrement a échoué
            if (data.fingerprintId) {
                config.fingerprint.fingerprints = (config.fingerprint.fingerprints || []).filter(f => f.id !== data.fingerprintId);
                saveConfig();
                updateFingerprintList();
                updateFingerprintStats();
            }
            break;
        case 'delete_success':
            if (feedback) {
                feedback.className = 'feedback-message success';
                feedback.textContent = 'Empreinte supprimée avec succès';
                feedback.style.display = 'block';
                setTimeout(() => { feedback.style.display = 'none'; }, 3000);
            }
            break;
        case 'list':
            if (data.fingerprints && Array.isArray(data.fingerprints)) {
                config.fingerprint.fingerprints = data.fingerprints;
                saveConfig();
                updateFingerprintList();
                updateFingerprintStats();
            }
            break;
        case 'match':
            // Empreinte reconnue
            if (data.fingerprint) {
                const fp = config.fingerprint.fingerprints.find(f => f.id === data.fingerprint.id);
                if (fp) {
                    fp.lastUsed = new Date().toISOString();
                    saveConfig();
                    updateFingerprintList();
                }
                
                // Si un profil est associé, switcher vers ce profil
                if (data.fingerprint.profileId && config.profiles[data.fingerprint.profileId]) {
                    config.activeProfile = data.fingerprint.profileId;
                    switchProfile();
                }
            }
            break;
        case 'status':
            updateFingerprintStatus(data.status || 'Prêt');
            break;
    }
}

// Envoyer la configuration de l'écran
async function sendDisplayConfig() {
    if (!config.connected) return;

    const modeEl = document.getElementById('display-screen-mode');
    if (modeEl) config.display.mode = modeEl.value;
    const cd = config.display.customData || {};
    ['display-show-profile','display-show-battery','display-show-mode','display-show-keys','display-show-backlight','display-show-custom1','display-show-custom2'].forEach((id, i) => {
        const k = ['showProfile','showBattery','showMode','showKeys','showBacklight','showCustom1','showCustom2'][i];
        const el = document.getElementById(id);
        if (el) cd[k] = el.checked;
    });
    const l1 = document.getElementById('display-custom-line1'); if (l1) cd.customLine1 = l1.value;
    const l2 = document.getElementById('display-custom-line2'); if (l2) cd.customLine2 = l2.value;
    config.display.customData = cd;

    const pay = { type: 'display', brightness: config.display.brightness, mode: config.display.mode, customData: cd };
    await sendDataToESP32(JSON.stringify(pay));

    if (config.display.mode === 'image' && config.display.imageData) {
        await sendDataToESP32(JSON.stringify({ type: 'display_image', frame: 0, total: 1, data: config.display.imageData }));
    } else if (config.display.mode === 'gif' && config.display.gifFrames && config.display.gifFrames.length) {
        const n = Math.min(config.display.gifFrames.length, 8);
        for (let i = 0; i < n; i++) {
            await sendDataToESP32(JSON.stringify({ type: 'display_image', frame: i, total: n, data: config.display.gifFrames[i] }));
        }
    }
}

// Fonction générique pour envoyer des données à l'ESP32
async function sendDataToESP32(data) {
    console.log('[DEBUG] [WEB_UI] Sending to ESP32:', data);
    try {
        if (config.connectionType === 'usb') {
            if (config.serialPort && config.serialPort.writable) {
                const writer = config.serialPort.writable.getWriter();
                await writer.write(new TextEncoder().encode(data + '\n'));
                writer.releaseLock();
                console.log('[DEBUG] [WEB_UI] Data sent via USB Serial');
            } else {
                console.warn('[DEBUG] [WEB_UI] Serial port not writable');
            }
        } else if (config.connectionType === 'bluetooth') {
            // Envoyer via Bluetooth
            if (config.bluetoothCharacteristic) {
                const encoder = new TextEncoder();
                const dataArray = encoder.encode(data + '\n');
                await config.bluetoothCharacteristic.writeValue(dataArray);
                console.log('[DEBUG] [WEB_UI] Data sent via Bluetooth');
            } else {
                console.error('[DEBUG] [WEB_UI] Bluetooth characteristic not available');
            }
        } else {
            console.warn('[DEBUG] [WEB_UI] Unknown connection type:', config.connectionType);
        }
    } catch (error) {
        console.error('[DEBUG] [WEB_UI] Error sending data:', error);
    }
}

// Mettre à jour les informations de l'écran
function updateDisplayInfo() {
    const keysCount = Object.keys(getCurrentKeys()).filter(id => id !== '0-0').length;
    const totalKeys = 16;
    const countStr = `${keysCount}/${totalKeys}`;
    
    const dkc = document.getElementById('display-keys-count');
    if (dkc) dkc.textContent = countStr;
    const ckc = document.getElementById('config-keys-count');
    if (ckc) ckc.textContent = countStr;
    
    const dm = document.getElementById('display-mode');
    if (dm) dm.textContent = (config.outputMode || 'usb').toUpperCase();
    const db = document.getElementById('display-backlight');
    if (db) db.textContent = config.backlight.enabled ? 'ON' : 'OFF';
    const dp = document.getElementById('display-profile');
    if (dp) dp.textContent = config.activeProfile || 'Profil 1';
    const sd = document.getElementById('screen-display-profile');
    if (sd) sd.textContent = config.activeProfile || 'Profil';
}

// Démarrer les mises à jour de statut
function startStatusUpdates() {
    updateDisplayInfo();
    
    statusUpdateInterval = setInterval(async () => {
        if (!config.connected) return;
        
        // Demander la luminosité ambiante si le mode environnement est activé
        if (config.backlight.envBrightness) {
            await sendDataToESP32(JSON.stringify({ type: 'get_light' }));
        }
        
        // Les mises à jour de statut seront reçues via les messages
        // depuis le port série ou Bluetooth dans handleESP32Message
    }, 2000); // Mise à jour toutes les 2 secondes
}

// --- Système de profils ---
function populateProfileSelect() {
    const sel = document.getElementById('profile-select');
    if (!sel) return;
    const names = Object.keys(config.profiles || {}).sort();
    sel.innerHTML = '';
    names.forEach(name => {
        const opt = document.createElement('option');
        opt.value = name;
        opt.textContent = name;
        if (name === config.activeProfile) opt.selected = true;
        sel.appendChild(opt);
    });
    const delBtn = document.getElementById('profile-delete');
    if (delBtn) delBtn.disabled = names.length <= 1;
    if (typeof lucide !== 'undefined') {
        lucide.createIcons();
    }
}

function switchToNextProfile() {
    ensureProfiles();
    const names = Object.keys(config.profiles);
    if (names.length === 0) return;
    const i = names.indexOf(config.activeProfile);
    config.activeProfile = names[(i + 1) % names.length];
    switchProfile();
}

function switchProfile() {
    ensureProfiles();
    if (!config.profiles[config.activeProfile]) return;
    selectedKey = null;
    const sd = document.getElementById('selected-key-display');
    if (sd) sd.textContent = 'Aucune';
    const kt = document.getElementById('key-type');
    if (kt) kt.value = 'key';
    const kl = document.getElementById('key-label');
    if (kl) kl.value = '';
    const keyValue = document.getElementById('key-value');
    const macroSequence = document.getElementById('macro-sequence');
    const modCtrl = document.getElementById('mod-ctrl');
    const modShift = document.getElementById('mod-shift');
    const modAlt = document.getElementById('mod-alt');
    const modGui = document.getElementById('mod-gui');
    if (keyValue) keyValue.value = '';
    if (macroSequence) macroSequence.value = '';
    if (modCtrl) modCtrl.checked = false;
    if (modShift) modShift.checked = false;
    if (modAlt) modAlt.checked = false;
    if (modGui) modGui.checked = false;
    updateFormVisibility();
    initializeGrid();
    updateDisplayInfo();
    saveConfig();
    if (config.connected) sendConfigToESP32();
}

function setupProfiles() {
    const sel = document.getElementById('profile-select');
    const addBtn = document.getElementById('profile-add');
    const dupBtn = document.getElementById('profile-duplicate');
    const delBtn = document.getElementById('profile-delete');
    
    populateProfileSelect();
    
    if (sel) sel.addEventListener('change', () => {
        config.activeProfile = sel.value;
        switchProfile();
    });
    
    if (addBtn) addBtn.addEventListener('click', () => {
        const n = Object.keys(config.profiles).length + 1;
        let name = prompt('Nom du nouveau profil:', 'Profil ' + n);
        if (!name) return;
        name = name.trim();
        if (!name) return;
        if (config.profiles[name]) { alert('Un profil avec ce nom existe déjà.'); return; }
        config.profiles[name] = { keys: {} };
        config.activeProfile = name;
        populateProfileSelect();
        switchProfile();
    });
    
    if (dupBtn) dupBtn.addEventListener('click', () => {
        let name = prompt('Nom de la copie:', config.activeProfile + ' (copie)');
        if (!name) return;
        name = name.trim();
        if (!name) return;
        if (config.profiles[name]) { alert('Un profil avec ce nom existe déjà.'); return; }
        const keysCopy = JSON.parse(JSON.stringify(getCurrentKeys()));
        NAV_DISPLAY_KEYS.forEach(id => delete keysCopy[id]);
        config.profiles[name] = { keys: keysCopy };
        config.activeProfile = name;
        populateProfileSelect();
        switchProfile();
    });
    
    if (delBtn) delBtn.addEventListener('click', () => {
        if (Object.keys(config.profiles).length <= 1) {
            alert('Impossible de supprimer le dernier profil.');
            return;
        }
        if (!confirm('Supprimer le profil « ' + config.activeProfile + ' » ?')) return;
        delete config.profiles[config.activeProfile];
        config.activeProfile = Object.keys(config.profiles)[0];
        populateProfileSelect();
        switchProfile();
    });
}

// Configurer le système d'onglets
function setupTabs() {
    const tabButtons = document.querySelectorAll('.tab-btn');
    const tabPanels = document.querySelectorAll('.tab-panel');
    
    if (tabButtons.length === 0 || tabPanels.length === 0) {
        console.warn('[TABS] Tab buttons or panels not found');
        return;
    }
    
    tabButtons.forEach(button => {
        button.addEventListener('click', (e) => {
            e.preventDefault();
            const targetTab = button.getAttribute('data-tab');
            if (!targetTab) return;
            
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabPanels.forEach(panel => panel.classList.remove('active'));
            
            button.classList.add('active');
            const targetId = 'tab-' + targetTab;
            const targetPanel = document.getElementById(targetId);
            if (targetPanel) {
                targetPanel.classList.add('active');
            }
            
            if (typeof createIcons !== 'undefined') {
                createIcons();
            }
        });
    });
}
