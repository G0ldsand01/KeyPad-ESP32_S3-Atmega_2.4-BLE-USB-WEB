/**
 * Bouton « Liste de souhaits » sur la fiche FlexPad
 */
const WISHLIST_KEY = 'flexpad_wishlist';

function readList() {
  try {
    const raw = localStorage.getItem(WISHLIST_KEY);
    const data = raw ? JSON.parse(raw) : [];
    return Array.isArray(data) ? data : [];
  } catch {
    return [];
  }
}

function writeList(items) {
  localStorage.setItem(WISHLIST_KEY, JSON.stringify(items));
}

function initWishlistBtn() {
  const btn = document.getElementById('btn-add-wishlist');
  if (!btn) return;
  if (btn.dataset.wishlistBound === '1') return;
  btn.dataset.wishlistBound = '1';

  const render = () => {
    const list = readList();
    const exists = list.some((x) => x && x.id === 'flexpad');
    btn.classList.toggle('is-active', exists);
    btn.textContent = exists ? '♥' : '♡';
    btn.setAttribute('aria-label', exists ? 'Retirer de la liste de souhaits' : 'Ajouter à la liste de souhaits');
    btn.setAttribute('title', exists ? 'Retirer de la liste de souhaits (navigateur)' : 'Enregistrer dans la liste de souhaits (navigateur)');
  };

  render();

  btn.addEventListener('click', () => {
    const list = readList();
    const exists = list.some((x) => x && x.id === 'flexpad');
    if (exists) {
      writeList(list.filter((x) => x && x.id !== 'flexpad'));
    } else {
      const entry = {
        id: 'flexpad',
        name: 'FlexPad',
        addedAt: new Date().toISOString(),
      };
      writeList(list.filter((x) => x && x.id !== 'flexpad').concat(entry));
    }
    render();
  });
}

document.addEventListener('DOMContentLoaded', initWishlistBtn);
document.addEventListener('astro:page-load', initWishlistBtn);
