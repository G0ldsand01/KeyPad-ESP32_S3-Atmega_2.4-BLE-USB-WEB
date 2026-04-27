import type { APIRoute } from 'astro';
import { randomUUID } from 'node:crypto';
import { getSession } from 'auth-astro/server';
import { normalizeCartPayload, taxesFromSubtotalCents } from '../../../lib/orders';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  const session = await getSession(request);
  if (!session?.user?.id) {
    return new Response(JSON.stringify({ error: 'Non authentifié' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'JSON invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const cartIn = (body as { cart?: unknown })?.cart;
  const normalized = normalizeCartPayload(cartIn);
  if (!normalized) {
    return new Response(JSON.stringify({ error: 'Panier invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const { tpsCents, tvqCents, totalCents } = taxesFromSubtotalCents(normalized.subtotalCents);

  const refRaw = (body as { reference?: string }).reference;
  const reference =
    typeof refRaw === 'string' && /^FXP-[A-Za-z0-9-]+$/.test(refRaw)
      ? refRaw
      : `FXP-${Date.now().toString(36).toUpperCase()}`;

  const shipping = (body as { shipping?: Record<string, string> }).shipping;
  const shippingJson =
    shipping && typeof shipping === 'object' ? JSON.stringify(shipping) : null;

  const id = randomUUID();

  // Mode sans DB (déploiement Vercel sans DATABASE_URL):
  // on confirme la commande sans persistance.

  return new Response(
    JSON.stringify({
      ok: true,
      orderId: id,
      reference,
      subtotalCents: normalized.subtotalCents,
      tpsCents,
      tvqCents,
      totalCents,
      shippingJson,
    }),
    {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
    }
  );
};
