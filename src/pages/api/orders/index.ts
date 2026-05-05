import type { APIRoute } from 'astro';
import { randomUUID } from 'node:crypto';
import { getSession } from 'auth-astro/server';
import { db } from '../../../db';
import { orders } from '../../../db/schema';
import { normalizeCartPayload, taxesFromSubtotalCents } from '../../../lib/orders';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  let session;
  try {
    session = await getSession(request);
  } catch {
    return new Response(JSON.stringify({ error: 'Session indisponible (AUTH_SECRET / Auth ou serveur).' }), {
      status: 503,
      headers: { 'Content-Type': 'application/json' },
    });
  }
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

  const dbUrl = process.env.DATABASE_URL?.trim();
  const dbEnabled = Boolean(dbUrl && /^postgres(ql)?:\/\//i.test(dbUrl));

  if (dbEnabled) {
    // Ne jamais bloquer le checkout si la DB est lente / indisponible.
    const write = db.insert(orders).values({
      id,
      userId: session.user.id,
      reference,
      status: 'confirmed',
      cartJson: JSON.stringify(normalized.items),
      shippingJson,
      subtotalCents: normalized.subtotalCents,
      tpsCents,
      tvqCents,
      totalCents,
    });

    const timeoutMs = 2500;
    const timeout = new Promise<null>((resolve) => setTimeout(() => resolve(null), timeoutMs));
    try {
      await Promise.race([write, timeout]);
    } catch {
      // On ignore l'erreur ici: le paiement peut continuer et le webhook/ops pourront réconcilier.
    }
  }

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
      dbEnabled,
    }),
    {
      status: 200,
      headers: { 'Content-Type': 'application/json' },
    }
  );
};
