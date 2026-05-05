/**
 * Thème global (localStorage + data-theme sur <html>) — toutes les pages.
 * Sur /config/, l’événement flexpad-theme-changed permet à macropad-app de persister dans macropadConfig.
 */
const THEME_STORAGE = 'theme';

function readDocTheme() {
  const raw = document.documentElement.getAttribute('data-theme');
  return raw === 'light' ? 'light' : 'dark';
}

function updateSiteThemeIcon(theme) {
  const icon = document.getElementById('site-theme-icon');
  if (!icon) return;
  icon.setAttribute('data-lucide', theme === 'dark' ? 'sun' : 'moon');
  if (typeof lucide !== 'undefined' && lucide.createIcons) {
    lucide.createIcons();
  }
}

function applyTheme(theme) {
  if (theme !== 'light' && theme !== 'dark') return;
  document.documentElement.setAttribute('data-theme', theme);
  try {
    localStorage.setItem(THEME_STORAGE, theme);
  } catch (_) {
    /* ignore */
  }
  updateSiteThemeIcon(theme);
  try {
    window.dispatchEvent(new CustomEvent('flexpad-theme-changed', { detail: { theme } }));
  } catch (_) {
    /* ignore */
  }
}

function bindSiteThemeToggle() {
  const btn = document.getElementById('site-theme-toggle');
  if (!btn) return;
  if (btn.dataset.themeBound === '1') {
    updateSiteThemeIcon(readDocTheme());
    return;
  }
  btn.dataset.themeBound = '1';
  updateSiteThemeIcon(readDocTheme());
  btn.addEventListener('click', (e) => {
    e.preventDefault();
    const cur = readDocTheme();
    applyTheme(cur === 'dark' ? 'light' : 'dark');
  });
}

function bootThemeUi() {
  try {
    const t = localStorage.getItem(THEME_STORAGE);
    if (t === 'light' || t === 'dark') {
      document.documentElement.setAttribute('data-theme', t);
    }
  } catch (_) {
    /* ignore */
  }
  bindSiteThemeToggle();
  if (typeof lucide !== 'undefined' && lucide.createIcons) {
    lucide.createIcons();
  }
}

document.addEventListener('astro:page-load', bootThemeUi);
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', bootThemeUi, { once: true });
} else {
  queueMicrotask(bootThemeUi);
}
