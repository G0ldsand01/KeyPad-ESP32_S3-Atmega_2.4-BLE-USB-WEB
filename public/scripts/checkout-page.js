/**
 * Page Commander — compatible View Transitions (astro:page-load)
 */

function whenDocumentReady(cb) {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', cb, { once: true });
  } else {
    queueMicrotask(cb);
  }
}

const CART_KEY = 'flexpad_cart';
const unitPrice = 149.99;

let checkoutAbort = null;

/** Parse une réponse fetch ; évite SyntaxError si Vercel renvoie du HTML (erreur 500). */
async function responseJsonOrThrow(res, label) {
  const ct = (res.headers.get('content-type') || '').toLowerCase();
  if (ct.includes('application/json')) {
    return res.json();
  }
  const text = await res.text();
  const hint = text.replace(/\s+/g, ' ').trim().slice(0, 120);
  throw new Error(
    hint || `${label}: HTTP ${res.status} (réponse non-JSON — vérifie les variables Vercel : AUTH_SECRET, STRIPE_SECRET_KEY, DATABASE_URL).`,
  );
}

function getCart() {
  try {
    const data = localStorage.getItem(CART_KEY);
    return data ? JSON.parse(data) : { items: [], total: 0 };
  } catch {
    return { items: [], total: 0 };
  }
}

function saveCartQty(qty) {
  const cart = getCart();
  const item = cart.items.find((i) => i.id === 'flexpad');
  if (qty <= 0) {
    cart.items = cart.items.filter((i) => i.id !== 'flexpad');
  } else if (item) {
    item.quantity = Math.max(1, qty);
  } else {
    cart.items.push({
      id: 'flexpad',
      name: 'FlexPad',
      tagline: 'Pavé numérique programmable',
      price: unitPrice,
      quantity: qty,
    });
  }
  cart.total = cart.items.reduce((s, i) => s + i.price * i.quantity, 0);
  if (cart.items.length === 0) {
    localStorage.removeItem(CART_KEY);
  } else {
    localStorage.setItem(CART_KEY, JSON.stringify(cart));
  }
  if (typeof window.updateCartBadge === 'function') window.updateCartBadge();
  // Ne pas appeler updateCartUI() ici : il dispatche flexpad:cart-updated → syncFromCart → updateTotal → saveCartQty (récursion).
  if (window.FlexPadCart?.refreshCartWidget) window.FlexPadCart.refreshCartWidget();
}

function clearCart() {
  localStorage.removeItem(CART_KEY);
  if (typeof window.updateCartBadge === 'function') window.updateCartBadge();
  if (window.FlexPadCart?.refreshCartWidget) window.FlexPadCart.refreshCartWidget();
}

function formatCad(value) {
  return new Intl.NumberFormat('fr-CA', {
    style: 'currency',
    currency: 'CAD',
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  }).format(value);
}

function initCheckoutPage() {
  if (checkoutAbort) {
    checkoutAbort.abort();
    checkoutAbort = null;
  }

  const form = document.getElementById('checkout-form');
  if (!form) return;

  checkoutAbort = new AbortController();
  const { signal } = checkoutAbort;

  const steps = document.querySelectorAll('.checkout-step');
  const stepIndicators = document.querySelectorAll('.step');
  const quantityInput = document.getElementById('quantity');
  const lineTotalPrice = document.getElementById('line-total');
  const subtotalEl = document.getElementById('checkout-subtotal');
  const tpsEl = document.getElementById('checkout-tps');
  const tvqEl = document.getElementById('checkout-tvq');
  const grandEl = document.getElementById('checkout-grand-total');
  const cartEmptyMsg = document.getElementById('cart-empty-msg');
  const cartContent = document.getElementById('checkout-cart-content');
  const removeBtn = document.getElementById('btn-remove-from-checkout');
  const btnToStep2 = document.getElementById('btn-to-step2');

  const cart = getCart();
  const flexpadItem = cart.items.find((i) => i.id === 'flexpad');
  const initialQty = flexpadItem ? flexpadItem.quantity : 1;

  if (cart.items.length === 0 && cartEmptyMsg && cartContent) {
    cartContent.hidden = true;
    cartEmptyMsg.hidden = false;
    if (btnToStep2 instanceof HTMLButtonElement) btnToStep2.disabled = true;
  } else if (quantityInput) {
    quantityInput.value = String(initialQty);
  }

  function syncEmptyState() {
    const cart = getCart();
    const empty = cart.items.length === 0;
    if (cartEmptyMsg && cartContent) {
      cartContent.hidden = empty;
      cartEmptyMsg.hidden = !empty;
    }
    if (btnToStep2 instanceof HTMLButtonElement) {
      btnToStep2.disabled = empty;
    }
  }

  function syncFromCart() {
    const cart = getCart();
    const item = cart.items.find((i) => i.id === 'flexpad');
    if (!item) {
      syncEmptyState();
      return;
    }

    // Ne pas écraser la saisie en cours.
    const active = document.activeElement;
    const isEditingQty = quantityInput && active === quantityInput;
    if (quantityInput && !isEditingQty) {
      quantityInput.value = String(item.quantity);
    }

    updateTotal(item.quantity);
    syncEmptyState();
  }

  function updateTotal(qtyOverride) {
    const raw = qtyOverride ?? parseInt(quantityInput?.value || '1', 10);
    const qty = Math.max(1, Number.isFinite(raw) ? raw : 1);
    const subtotal = unitPrice * qty;
    const tps = subtotal * 0.05;
    const tvq = subtotal * 0.09975;
    const grand = subtotal + tps + tvq;
    if (lineTotalPrice) lineTotalPrice.textContent = formatCad(subtotal);
    if (subtotalEl) subtotalEl.textContent = formatCad(subtotal);
    if (tpsEl) tpsEl.textContent = formatCad(tps);
    if (tvqEl) tvqEl.textContent = formatCad(tvq);
    if (grandEl) grandEl.textContent = formatCad(grand);
    if (getCart().items.length > 0) saveCartQty(qty);
  }

  function showStep(stepNum) {
    steps.forEach((s, i) => {
      s.classList.toggle('hidden', i + 1 !== stepNum);
    });
    stepIndicators.forEach((s, i) => {
      s.classList.toggle('active', i + 1 === stepNum);
    });
  }

  quantityInput?.addEventListener(
    'input',
    () => {
      // Laisser l'utilisateur vider temporairement le champ sans forcer "1"
      if (!quantityInput) return;
      if (quantityInput.value === '') return;
      const raw = parseInt(quantityInput.value, 10);
      const qty = Number.isFinite(raw) ? Math.max(1, raw) : 1;
      updateTotal(qty);
    },
    { signal }
  );

  quantityInput?.addEventListener(
    'change',
    () => {
      if (!quantityInput) return;
      const raw = parseInt(quantityInput.value || '1', 10);
      const qty = Number.isFinite(raw) ? Math.max(1, raw) : 1;
      quantityInput.value = String(qty);
      updateTotal(qty);
    },
    { signal }
  );

  document.getElementById('btn-to-step2')?.addEventListener(
    'click',
    () => {
      if (getCart().items.length === 0) return;
      showStep(2);
    },
    { signal }
  );

  removeBtn?.addEventListener(
    'click',
    () => {
      // Utiliser le même modal que le popover
      window.FlexPadCart?.showRemoveOverlay?.('flexpad');
    },
    { signal }
  );

  document.addEventListener('flexpad:cart-updated', syncFromCart, { signal });

  document.getElementById('btn-back-step2')?.addEventListener('click', () => showStep(1), { signal });

  form.addEventListener(
    'submit',
    async (e) => {
      e.preventDefault();
      const ref = 'FXP-' + Math.floor(1000 + Math.random() * 9000);
      const orderRefEl = document.getElementById('order-ref');
      const qty = Math.max(1, Math.min(10, parseInt(quantityInput?.value || '1', 10)));
      const cart = getCart();
      const fd = new FormData(form);
      const shipping = {
        name: String(fd.get('name') || ''),
        email: String(fd.get('email') || ''),
        phone: String(fd.get('phone') || ''),
        address: String(fd.get('address') || ''),
        city: String(fd.get('city') || ''),
        postal: String(fd.get('postal') || ''),
      };
      let displayRef = ref;
      let createdOrderId = null;
      try {
        const sessRes = await fetch('/api/auth/session', { credentials: 'same-origin' });
        const sess = await sessRes.json();
        if (sess && sess.user) {
          const subtotal = unitPrice * qty;
          const tps = subtotal * 0.05;
          const tvq = subtotal * 0.09975;
          const cartPayload = {
            items: cart.items.length
              ? cart.items
              : [
                  {
                    id: 'flexpad',
                    name: 'FlexPad',
                    tagline: 'Pavé numérique programmable',
                    price: unitPrice,
                    quantity: qty,
                  },
                ],
            total: subtotal,
          };
          const saveRes = await fetch('/api/orders', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
              reference: ref,
              cart: cartPayload,
              shipping,
            }),
          });
          if (saveRes.ok) {
            const j = await responseJsonOrThrow(saveRes, 'Commande');
            if (j.reference) displayRef = j.reference;
            if (j.orderId) createdOrderId = j.orderId;
          }
        }
      } catch (err) {
        console.warn('[checkout] enregistrement commande', err);
      }
      if (orderRefEl) orderRefEl.textContent = displayRef;

      // Démarrer le vrai paiement Stripe (redirige vers Checkout)
      try {
        try {
          sessionStorage.setItem('flexpad_last_reference', displayRef);
          if (createdOrderId) sessionStorage.setItem('flexpad_last_order_id', String(createdOrderId));
        } catch {}

        const cartPayload = {
          items: cart.items.length
            ? cart.items
            : [
                {
                  id: 'flexpad',
                  name: 'FlexPad',
                  tagline: 'Pavé numérique programmable',
                  price: unitPrice,
                  quantity: qty,
                },
              ],
          total: unitPrice * qty,
        };
        const stripeRes = await fetch('/api/stripe/checkout-session', {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            orderId: createdOrderId,
            reference: displayRef,
            cart: cartPayload,
          }),
        });
        const stripeJson = await responseJsonOrThrow(stripeRes, 'Stripe');
        if (stripeRes.ok && stripeJson && stripeJson.url) {
          try {
            if (stripeJson.reference) sessionStorage.setItem('flexpad_last_reference', String(stripeJson.reference));
          } catch {}
          window.location.href = stripeJson.url;
          return;
        }
        const msg =
          (stripeJson && (stripeJson.error || stripeJson.message)) ||
          `Erreur paiement (HTTP ${stripeRes.status})`;
        alert(msg);
        return;
      } catch (err) {
        console.warn('[checkout] stripe', err);
        alert("Paiement indisponible pour l'instant. Réessaie dans quelques secondes.");
        return;
      }

      // Ne jamais montrer la confirmation si le paiement n'a pas démarré.
    },
    { signal }
  );

  if (cart.items.length > 0) updateTotal();
  syncEmptyState();
}

document.addEventListener('astro:page-load', initCheckoutPage);
whenDocumentReady(initCheckoutPage);
