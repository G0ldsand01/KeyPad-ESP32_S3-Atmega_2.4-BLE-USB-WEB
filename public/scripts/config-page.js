/**
 * Page /config/ — même rôle que l’ancien script inline : initApp (macropad-app.js).
 * Chargement dynamique pour ne pas télécharger ~140 ko sur les pages vitrine.
 * View Transitions : réinit à chaque astro:page-load sur l’onglet principal.
 *
 * Chargement direct (URL /config/) : `astro:page-load` peut déjà être passé avant
 * ce module (scripts en fin de <body>), donc on lance aussi un boot explicite.
 * Jeton : évite d’appeler initApp deux fois si les deux chemins arrivent en même temps.
 */

let lucideThemeObserver = null;
let macropadBootToken = 0;

function setupLucideThemeObserver() {
  if (lucideThemeObserver) {
    lucideThemeObserver.disconnect();
    lucideThemeObserver = null;
  }
  lucideThemeObserver = new MutationObserver(() => {
    if (typeof lucide !== 'undefined' && lucide.createIcons) {
      lucide.createIcons();
    }
  });
  lucideThemeObserver.observe(document.documentElement, {
    attributes: true,
    attributeFilter: ['data-theme'],
  });
}

async function bootMacropad() {
  if (!document.getElementById('tab-main')) {
    if (lucideThemeObserver) {
      lucideThemeObserver.disconnect();
      lucideThemeObserver = null;
    }
    return;
  }
  const token = ++macropadBootToken;
  const { initApp } = await import('./macropad-app.js');
  if (token !== macropadBootToken) return;
  initApp();
  setupLucideThemeObserver();
}

document.addEventListener('astro:page-load', bootMacropad);
void bootMacropad();
