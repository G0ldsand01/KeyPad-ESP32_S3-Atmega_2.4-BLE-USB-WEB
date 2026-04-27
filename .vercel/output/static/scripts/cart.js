/**
 * FlexPad — Panier, aperçu, overlay « ajouté au panier »
 * Réinit sur astro:page-load (View Transitions)
 */

function whenDocumentReady(cb) {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', cb, { once: true });
  } else {
    queueMicrotask(cb);
  }
}

const CART_KEY = 'flexpad_cart';
const PRODUCT = {
  id: 'flexpad',
  name: 'FlexPad',
  tagline: 'Pavé numérique programmable',
  price: 149.99,
};

let cartAbort = null;
let pendingRemoveId = null;

function getCart() {
  try {
    const data = localStorage.getItem(CART_KEY);
    return data ? JSON.parse(data) : { items: [], total: 0 };
  } catch {
    return { items: [], total: 0 };
  }
}

function saveCart(cart) {
  cart.total = cart.items.reduce((sum, i) => sum + i.price * i.quantity, 0);
  localStorage.setItem(CART_KEY, JSON.stringify(cart));
  return cart;
}

function addToCart(quantity = 1) {
  const cart = getCart();
  const existing = cart.items.find((i) => i.id === PRODUCT.id);
  if (existing) {
    existing.quantity = existing.quantity + quantity;
  } else {
    cart.items.push({
      id: PRODUCT.id,
      name: PRODUCT.name,
      tagline: PRODUCT.tagline,
      price: PRODUCT.price,
      quantity,
    });
  }
  saveCart(cart);
  updateCartUI();
  return cart;
}

function removeFromCart() {
  const cart = getCart();
  cart.items = cart.items.filter((i) => i.id !== PRODUCT.id);
  saveCart(cart);
  updateCartUI();
  return cart;
}

function setQuantity(quantity) {
  const cart = getCart();
  const item = cart.items.find((i) => i.id === PRODUCT.id);
  if (item) {
    item.quantity = Math.max(1, quantity);
    saveCart(cart);
    updateCartUI();
  }
  return cart;
}

function setItemQuantityById(id, quantity) {
  const cart = getCart();
  const item = cart.items.find((i) => i.id === id);
  if (!item) return cart;

  const q = Math.max(0, quantity);
  if (q === 0) {
    showCartRemoveOverlay(id);
    return cart;
  }

  item.quantity = q;
  saveCart(cart);
  updateCartUI();
  return cart;
}

function removeItemById(id) {
  const cart = getCart();
  cart.items = cart.items.filter((i) => i.id !== id);
  if (cart.items.length === 0) {
    localStorage.removeItem(CART_KEY);
  } else {
    saveCart(cart);
  }
  updateCartUI();
  return cart;
}

function updateCartBadge() {
  const badge = document.getElementById('cart-badge');
  const cart = getCart();
  const count = cart.items.reduce((sum, i) => sum + i.quantity, 0);
  if (badge) {
    badge.textContent = String(count);
    badge.classList.toggle('cart-badge--visible', count > 0);
  }
}

function renderCartPreview() {
  const emptyEl = document.getElementById('cart-empty');
  const itemsEl = document.getElementById('cart-items');
  const footerEl = document.getElementById('cart-dropdown-footer');
  const totalEl = document.getElementById('cart-total-price');

  if (!emptyEl || !itemsEl || !footerEl) return;

  const cart = getCart();

  if (cart.items.length === 0) {
    emptyEl.hidden = false;
    itemsEl.hidden = true;
    footerEl.hidden = true;
    return;
  }

  emptyEl.hidden = true;
  itemsEl.hidden = false;
  footerEl.hidden = false;

  itemsEl.innerHTML = cart.items
    .map(
      (item) => `
    <div class="cart-preview-item" data-id="${item.id}">
      <div class="cart-preview-image">
        <div class="numpad-preview-mini">
          <div class="numpad-preview-screen">FlexPad</div>
          <div class="numpad-preview-keys"></div>
        </div>
      </div>
      <div class="cart-preview-details">
        <h4>${item.name}</h4>
        <p>${item.tagline}</p>
        <div class="cart-preview-qty">
          <div class="cart-preview-qty-controls" aria-label="Quantité">
            <button type="button" class="cart-qty-btn" data-cart-qty-minus="${item.id}" aria-label="Diminuer la quantité">−</button>
            <input
              type="number"
              class="cart-qty-input"
              inputmode="numeric"
              min="0"
              value="${item.quantity}"
              aria-label="Quantité"
              data-cart-qty-input="${item.id}"
            />
            <button type="button" class="cart-qty-btn" data-cart-qty-plus="${item.id}" aria-label="Augmenter la quantité">+</button>
          </div>
          <span class="cart-preview-price">${(item.price * item.quantity).toFixed(2)} $</span>
        </div>
        <div class="cart-preview-actions">
          <button type="button" class="btn-danger btn-sm" data-cart-remove="${item.id}" aria-label="Retirer du panier">
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <path d="M3 6h18"/>
              <path d="M8 6V4h8v2"/>
              <path d="M6 6l1 16h10l1-16"/>
              <path d="M10 11v6"/>
              <path d="M14 11v6"/>
            </svg>
            Retirer
          </button>
        </div>
      </div>
    </div>
  `
    )
    .join('');

  if (totalEl) {
    totalEl.textContent = `${cart.total.toFixed(2)} $`;
  }
}

function updateCartUI() {
  updateCartBadge();
  renderCartPreview();
  document.dispatchEvent(new CustomEvent('flexpad:cart-updated'));
}

let toastTimer = null;

function hideCartAddedToast() {
  const toast = document.getElementById('cart-added-toast');
  if (!toast || toast.hidden) return;
  toast.classList.remove('is-entering');
  toast.classList.add('is-leaving');
  if (toastTimer) {
    clearTimeout(toastTimer);
    toastTimer = null;
  }

  const done = () => {
    toast.hidden = true;
    toast.classList.remove('is-leaving');
    toast.removeEventListener('animationend', done);
  };

  // Si l’animation est désactivée, on cache immédiatement.
  const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  if (reduce) {
    done();
    return;
  }

  toast.addEventListener('animationend', done);
  // fallback si l’événement ne part pas
  setTimeout(done, 260);
}

function hideCartRemoveOverlay() {
  const overlay = document.getElementById('cart-remove-overlay');
  if (overlay) overlay.hidden = true;
  document.documentElement.classList.remove('cart-overlay-open');
  pendingRemoveId = null;
}

function showCartRemoveOverlay(itemId) {
  const overlay = document.getElementById('cart-remove-overlay');
  const summary = document.getElementById('cart-remove-summary');
  if (!overlay) return;

  pendingRemoveId = itemId;

  const cart = getCart();
  const item = cart.items.find((i) => i.id === itemId);
  if (summary && item) {
    summary.textContent = `Voulez-vous vraiment retirer « ${item.name} » de votre panier ?`;
  }

  overlay.hidden = false;
  document.documentElement.classList.add('cart-overlay-open');
  const focusEl = overlay.querySelector('[data-cart-remove-primary]');
  if (focusEl instanceof HTMLElement) focusEl.focus();
}

function showCartAddedOverlay(qtyJustAdded) {
  const toast = document.getElementById('cart-added-toast');
  const summary = document.getElementById('cart-toast-summary');
  if (!toast || !summary) return;

  const cart = getCart();
  const item = cart.items.find((i) => i.id === PRODUCT.id);
  const q = item ? item.quantity : 0;
  const line = qtyJustAdded > 1
    ? `${qtyJustAdded} article(s) ajouté(s). Quantité totale : ${q}.`
    : `Article ajouté. Quantité dans le panier : ${q}.`;
  summary.textContent = `${line} Sous-total : ${cart.total.toFixed(2)} $ CAD.`;

  toast.hidden = false;
  toast.classList.remove('is-entering');
  // relance l’animation à chaque ajout
  // eslint-disable-next-line no-unused-expressions
  toast.offsetHeight;
  toast.classList.add('is-entering');
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => hideCartAddedToast(), 2500);
}

function openCartDropdown() {
  const trigger = document.getElementById('cart-trigger');
  const dropdown = document.getElementById('cart-dropdown');
  if (trigger && dropdown) {
    dropdown.classList.add('cart-dropdown--open');
    trigger.setAttribute('aria-expanded', 'true');
  }
}

window.FlexPadCart = {
  addToCart,
  removeFromCart,
  setQuantity,
  setItemQuantityById,
  showRemoveOverlay: showCartRemoveOverlay,
  getCart,
  updateCartBadge,
  updateCartUI,
};
window.updateCartBadge = updateCartBadge;

function bindCartDropdown(signal) {
  const trigger = document.getElementById('cart-trigger');
  const dropdown = document.getElementById('cart-dropdown');
  if (!trigger || !dropdown) return;

  // IMPORTANT: on re-render les items après un clic (qty +/-),
  // donc le "click outside" peut croire que le clic était dehors.
  dropdown.addEventListener(
    'click',
    (e) => e.stopPropagation(),
    { signal }
  );

  const close = () => {
    dropdown.classList.remove('cart-dropdown--open');
    trigger.setAttribute('aria-expanded', 'false');
  };

  trigger.addEventListener(
    'click',
    (e) => {
      e.stopPropagation();
      const open = !dropdown.classList.contains('cart-dropdown--open');
      dropdown.classList.toggle('cart-dropdown--open', open);
      trigger.setAttribute('aria-expanded', open ? 'true' : 'false');
    },
    { signal }
  );

  document.addEventListener(
    'click',
    (e) => {
      const t = e.target;
      if (!(t instanceof Node)) return;
      if (!dropdown.contains(t) && !trigger.contains(t)) close();
    },
    { signal }
  );

  document.addEventListener(
    'keydown',
    (e) => {
      if (e.key === 'Escape') {
        const t = document.getElementById('cart-added-toast');
        if (t && !t.hidden) hideCartAddedToast();
        close();
      }
    },
    { signal }
  );
}

function bindCartToast(signal) {
  const toast = document.getElementById('cart-added-toast');
  if (!toast) return;

  toast.querySelectorAll('[data-cart-toast-close]').forEach((el) => {
    el.addEventListener('click', () => hideCartAddedToast(), { signal });
  });
  // "Voir le panier" est un lien vers /checkout/
}

function bindRemoveOverlay(signal) {
  const overlay = document.getElementById('cart-remove-overlay');
  if (!overlay) return;

  overlay.querySelectorAll('[data-cart-remove-close]').forEach((el) => {
    el.addEventListener('click', () => hideCartRemoveOverlay(), { signal });
  });

  const confirmBtn = document.getElementById('cart-remove-confirm');
  confirmBtn?.addEventListener(
    'click',
    () => {
      if (pendingRemoveId) removeItemById(pendingRemoveId);
      hideCartRemoveOverlay();
    },
    { signal }
  );
}

function bindAddToCartButtons(signal) {
  document.querySelectorAll('#btn-add-to-cart, #btn-add-to-cart-home').forEach((btn) => {
    btn.addEventListener(
      'click',
      () => {
        const qtyInput = document.getElementById('product-quantity');
        const qty = qtyInput
          ? Math.max(1, Math.min(10, parseInt(qtyInput.value || '1', 10)))
          : 1;
        addToCart(qty);
        showCartAddedOverlay(qty);
        const originalText = btn.textContent;
        btn.textContent = 'Ajouté !';
        btn.disabled = true;
        setTimeout(() => {
          btn.textContent = originalText;
          btn.disabled = false;
        }, 1200);
      },
      { signal }
    );
  });
}

function bindRemoveFromCartButtons(signal) {
  const itemsEl = document.getElementById('cart-items');
  if (!itemsEl) return;

  itemsEl.addEventListener(
    'click',
    (e) => {
      const t = e.target;
      if (!(t instanceof HTMLElement)) return;

      const minus = t.closest('[data-cart-qty-minus]');
      if (minus instanceof HTMLElement) {
        const id = minus.getAttribute('data-cart-qty-minus');
        if (!id) return;
        const cart = getCart();
        const item = cart.items.find((i) => i.id === id);
        if (!item) return;
        setItemQuantityById(id, item.quantity - 1);
        return;
      }

      const plus = t.closest('[data-cart-qty-plus]');
      if (plus instanceof HTMLElement) {
        const id = plus.getAttribute('data-cart-qty-plus');
        if (!id) return;
        const cart = getCart();
        const item = cart.items.find((i) => i.id === id);
        if (!item) return;
        setItemQuantityById(id, item.quantity + 1);
        return;
      }

      const btn = t.closest('[data-cart-remove]');
      if (!(btn instanceof HTMLElement)) return;
      const id = btn.getAttribute('data-cart-remove');
      if (!id) return;
      showCartRemoveOverlay(id);
    },
    { signal }
  );
}

function bindQuantityInputs(signal) {
  const itemsEl = document.getElementById('cart-items');
  if (!itemsEl) return;

  itemsEl.addEventListener(
    'change',
    (e) => {
      const t = e.target;
      if (!(t instanceof HTMLInputElement)) return;
      const id = t.getAttribute('data-cart-qty-input');
      if (!id) return;
      const raw = parseInt(t.value || '0', 10);
      const q = Number.isFinite(raw) ? raw : 0;
      setItemQuantityById(id, q);
    },
    { signal }
  );
}

function initCart() {
  if (!document.getElementById('flexpad-cart-widget')) {
    document.dispatchEvent(new CustomEvent('flexpad:cart-ready', { bubbles: true }));
    return;
  }
  if (cartAbort) cartAbort.abort();
  cartAbort = new AbortController();
  const { signal } = cartAbort;

  bindCartDropdown(signal);
  bindCartToast(signal);
  bindRemoveOverlay(signal);
  bindAddToCartButtons(signal);
  bindRemoveFromCartButtons(signal);
  bindQuantityInputs(signal);
  updateCartUI();
  document.dispatchEvent(new CustomEvent('flexpad:cart-ready', { bubbles: true }));
}

document.addEventListener('astro:page-load', initCart);
whenDocumentReady(initCart);
