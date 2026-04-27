/** Prix catalogue serveur (ne pas faire confiance au client pour le montant) */
export const FLEXPAD_UNIT_CENTS = 14999;

export type CartLine = {
  id: string;
  name: string;
  tagline?: string;
  price: number;
  quantity: number;
};

export type NormalizedCart = {
  items: CartLine[];
  subtotalCents: number;
};

export function normalizeCartPayload(cart: unknown): NormalizedCart | null {
  if (!cart || typeof cart !== 'object') return null;
  const itemsRaw = (cart as { items?: unknown }).items;
  if (!Array.isArray(itemsRaw) || itemsRaw.length === 0) return null;

  const items: CartLine[] = [];
  let subtotalCents = 0;

  for (const row of itemsRaw) {
    if (!row || typeof row !== 'object') continue;
    const id = (row as { id?: string }).id;
    if (id !== 'flexpad') continue;
    const q = Math.max(1, Math.floor(Number((row as { quantity?: number }).quantity) || 0));
    if (q < 1) continue;
    const name = String((row as { name?: string }).name || 'FlexPad');
    const tagline = (row as { tagline?: string }).tagline;
    const lineCents = FLEXPAD_UNIT_CENTS * q;
    subtotalCents += lineCents;
    items.push({
      id: 'flexpad',
      name,
      tagline: typeof tagline === 'string' ? tagline : 'Pavé numérique programmable',
      price: FLEXPAD_UNIT_CENTS / 100,
      quantity: q,
    });
  }

  if (items.length === 0) return null;
  return { items, subtotalCents };
}

export function taxesFromSubtotalCents(subtotalCents: number): {
  tpsCents: number;
  tvqCents: number;
  totalCents: number;
} {
  const sub = subtotalCents / 100;
  const tpsCents = Math.round(sub * 0.05 * 100);
  const tvqCents = Math.round(sub * 0.09975 * 100);
  return {
    tpsCents,
    tvqCents,
    totalCents: subtotalCents + tpsCents + tvqCents,
  };
}

export function formatCadFromCents(cents: number): string {
  return new Intl.NumberFormat('fr-CA', {
    style: 'currency',
    currency: 'CAD',
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  }).format(cents / 100);
}

const STATUS = ['confirmed', 'processing', 'shipped', 'cancelled'] as const;
export type OrderStatus = (typeof STATUS)[number];

export function parseOrderStatus(raw: string | undefined): OrderStatus | null {
  if (!raw) return null;
  return STATUS.includes(raw as OrderStatus) ? (raw as OrderStatus) : null;
}
