import type { APIRoute } from 'astro';
import { eq } from 'drizzle-orm';
import { getSession } from 'auth-astro/server';
import { db } from '../../../db';
import { orders } from '../../../db/schema';
import { stripeCreateRefundForPaymentIntent } from '../../../lib/stripe-http';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  const url = process.env.DATABASE_URL?.trim();
  if (!url || !/^postgres(ql)?:\/\//i.test(url)) {
    return new Response(JSON.stringify({ error: 'Base de données désactivée' }), {
      status: 503,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const session = await getSession(request);
  if (!session?.user || session.user.role !== 'admin') {
    return new Response(JSON.stringify({ error: 'Interdit' }), {
      status: 403,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  let body: { orderId?: string };
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'JSON invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const orderId = typeof body.orderId === 'string' ? body.orderId : '';
  if (!orderId) {
    return new Response(JSON.stringify({ error: 'orderId requis' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const [row] = await db.select().from(orders).where(eq(orders.id, orderId)).limit(1);
  if (!row) {
    return new Response(JSON.stringify({ error: 'Commande introuvable' }), {
      status: 404,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const pi = row.stripePaymentIntentId?.trim();
  if (!pi) {
    return new Response(
      JSON.stringify({
        error:
          'Aucun paiement Stripe (PaymentIntent) associé. Vérifie que le webhook checkout.session.completed a tourné.',
      }),
      { status: 400, headers: { 'Content-Type': 'application/json' } },
    );
  }

  if (row.refundedCents >= row.totalCents && row.totalCents > 0) {
    return new Response(JSON.stringify({ error: 'Cette commande est déjà entièrement remboursée.' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const connectedAccount = (
    import.meta.env.STRIPE_CONNECTED_ACCOUNT_ID ||
    process.env.STRIPE_CONNECTED_ACCOUNT_ID ||
    ''
  ).trim();

  try {
    const refund = await stripeCreateRefundForPaymentIntent(
      pi,
      connectedAccount ? { stripeAccount: connectedAccount } : {},
    );
    await db
      .update(orders)
      .set({
        refundedCents: row.totalCents,
        status: 'refunded',
      })
      .where(eq(orders.id, orderId));

    return new Response(
      JSON.stringify({
        ok: true,
        refundId: refund.id,
        stripeStatus: refund.status,
      }),
      { status: 200, headers: { 'Content-Type': 'application/json' } },
    );
  } catch (err) {
    return new Response(JSON.stringify({ error: (err as Error).message }), {
      status: 502,
      headers: { 'Content-Type': 'application/json' },
    });
  }
};
