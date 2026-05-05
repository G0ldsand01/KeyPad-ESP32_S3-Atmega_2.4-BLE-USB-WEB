import type { APIRoute } from 'astro';
import { eq } from 'drizzle-orm';
import { db } from '../../../db';
import { orders } from '../../../db/schema';
import { verifyStripeWebhookSignature } from '../../../lib/stripe-http';

export const prerender = false;

function requireWebhookSecret(): string {
  const secret = (import.meta.env.STRIPE_WEBHOOK_SECRET || process.env.STRIPE_WEBHOOK_SECRET || '').trim();
  if (!secret) throw new Error('STRIPE_WEBHOOK_SECRET manquant dans .env');
  return secret;
}

export const POST: APIRoute = async ({ request }) => {
  const sig = request.headers.get('stripe-signature') || '';
  const rawBody = await request.text();

  try {
    verifyStripeWebhookSignature(rawBody, sig, requireWebhookSecret());
  } catch (err) {
    return new Response(JSON.stringify({ error: (err as Error).message }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  let event: any;
  try {
    event = JSON.parse(rawBody);
  } catch {
    return new Response(JSON.stringify({ error: 'JSON invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const dbUrl = process.env.DATABASE_URL?.trim();
  const dbEnabled = Boolean(dbUrl && /^postgres(ql)?:\/\//i.test(dbUrl));

  // Minimal: marquer une commande payée quand le Checkout est complété
  if (dbEnabled && event?.type === 'checkout.session.completed') {
    const session = event.data?.object;
    const orderId = typeof session?.metadata?.order_id === 'string' ? session.metadata.order_id : '';
    const reference = typeof session?.metadata?.reference === 'string' ? session.metadata.reference : '';
    const paymentIntent = typeof session?.payment_intent === 'string' ? session.payment_intent : null;
    const checkoutSessionId = typeof session?.id === 'string' ? session.id : null;

    if (orderId) {
      await db
        .update(orders)
        .set({
          stripeCheckoutSessionId: checkoutSessionId,
          stripePaymentIntentId: paymentIntent,
          paidAt: new Date(),
          status: 'processing',
        })
        .where(eq(orders.id, orderId));
    } else if (checkoutSessionId && reference) {
      // Fallback: si on n'a pas l'orderId (checkout invité), on peut quand même relier par référence si elle existe.
      await db
        .update(orders)
        .set({
          stripeCheckoutSessionId: checkoutSessionId,
          stripePaymentIntentId: paymentIntent,
          paidAt: new Date(),
          status: 'processing',
        })
        .where(eq(orders.reference, reference));
    }
  }

  // Blueprint “subscription”: invoice.payment_succeeded
  // (pour l’instant on ne modifie pas d’autres tables, mais l’event est accepté)

  return new Response(JSON.stringify({ received: true }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

