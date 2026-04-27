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

function macropadModuleSpecifier() {
  // Évite les résolutions relatives bizarres quand ce fichier est servi avec ?v=… (cache-bust) :
  // en prod (Vercel) certains navigateurs résolvaient mal ./macropad-app.js.
  if (typeof import.meta.url === 'string' && import.meta.url.length > 0) {
    return new URL('./macropad-app.js', import.meta.url).href;
  }
  return '/scripts/macropad-app.js';
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
  let initApp;
  const spec = macropadModuleSpecifier();
  try {
    ({ initApp } = await import(spec));
  } catch (e) {
    console.error('[config-page] import dynamique échoué:', spec, e);
    try {
      ({ initApp } = await import('/scripts/macropad-app.js'));
    } catch (e2) {
      console.error('[config-page] repli /scripts/macropad-app.js échoué:', e2);
      return;
    }
  }
  if (token !== macropadBootToken) return;
  try {
    initApp();
  } catch (e) {
    console.error('[config-page] initApp:', e);
    return;
  }
  setupLucideThemeObserver();
}

document.addEventListener('astro:page-load', bootMacropad);
void bootMacropad();
