import type { APIRoute } from 'astro';
import { eq } from 'drizzle-orm';
import { getSession } from 'auth-astro/server';
import { db } from '../../../db';
import { orders } from '../../../db/schema';
import { normalizeCartPayload, taxesFromSubtotalCents } from '../../../lib/orders';
import { stripePost } from '../../../lib/stripe-http';

export const prerender = false;

function generateOrderNumber(): string {
  // Lisible + unique: FXP-YYYYMMDD-XXXXXX
  const d = new Date();
  const y = String(d.getFullYear());
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  const rand = Math.random().toString(36).slice(2, 8).toUpperCase();
  return `FXP-${y}${m}${day}-${rand}`;
}

async function runCheckoutSessionPost({ request, url }: { request: Request; url: URL }): Promise<Response> {
  // Checkout invité autorisé: session optionnelle.
  const session = await getSession(request).catch(() => null);
  const userId = session?.user?.id ?? '';
  const userEmail = session?.user?.email ?? '';

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
  const orderId = typeof (body as { orderId?: unknown }).orderId === 'string' ? (body as { orderId: string }).orderId : null;
  const refIn = (body as { reference?: unknown }).reference;
  const reference =
    typeof refIn === 'string' && /^FXP-[A-Za-z0-9-]+$/.test(refIn) ? refIn : generateOrderNumber();

  const currency = ((import.meta.env.STRIPE_CURRENCY || process.env.STRIPE_CURRENCY || 'CAD') as string).trim().toLowerCase();
  const connectedAccount = (import.meta.env.STRIPE_CONNECTED_ACCOUNT_ID || process.env.STRIPE_CONNECTED_ACCOUNT_ID || '').trim();
  const appFeeCents =
    Number.parseInt(
      ((import.meta.env.STRIPE_APPLICATION_FEE_CENTS || process.env.STRIPE_APPLICATION_FEE_CENTS || '0') as string).trim(),
      10,
    ) || 0;

  const successUrl = `${url.origin}/checkout/success?session_id={CHECKOUT_SESSION_ID}&ref=${encodeURIComponent(reference)}`;
  const cancelUrl = `${url.origin}/checkout/`;

  let created: { id: string; url?: string; payment_intent?: string };
  try {
    const paymentIntentData: Record<string, unknown> = {
      // Se reflète dans le PaymentIntent et généralement dans le Charge (colonne Description des transactions)
      description: `Commande ${reference} — FlexPad`,
      metadata: {
        order_number: reference,
        reference,
        order_id: orderId ?? '',
        user_id: userId,
      },
    };

    // Stripe Connect (si activé): ajouter les frais d'application sans perdre la description/metadata.
    if (connectedAccount && appFeeCents > 0) {
      paymentIntentData.application_fee_amount = appFeeCents;
    }

    created = await stripePost<{ id: string; url?: string; payment_intent?: string }>(
      '/v1/checkout/sessions',
      {
        client_reference_id: reference,
        success_url: successUrl,
        cancel_url: cancelUrl,
        line_items: [
          {
            price_data: {
              currency,
              product_data: { name: 'FlexPad' },
              unit_amount: totalCents,
            },
            quantity: 1,
          },
        ],
        mode: 'payment',
        payment_method_types: ['card'],
        // Important: toujours définir payment_intent_data pour que la description soit visible dans Stripe.
        payment_intent_data: paymentIntentData,
        ...(userEmail ? { customer_email: userEmail } : null),
        metadata: {
          order_number: reference,
          order_id: orderId ?? '',
          reference,
          user_id: userId,
          tps_cents: String(tpsCents),
          tvq_cents: String(tvqCents),
        },
      },
      connectedAccount ? { stripeAccount: connectedAccount } : {},
    );
  } catch (err) {
    return new Response(JSON.stringify({ error: (err as Error).message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const dbUrl = process.env.DATABASE_URL?.trim();
  const dbEnabled = Boolean(dbUrl && /^postgres(ql)?:\/\//i.test(dbUrl));
  if (dbEnabled && orderId) {
    try {
      await db
        .update(orders)
        .set({
          stripeCheckoutSessionId: created.id,
          stripePaymentIntentId: typeof created.payment_intent === 'string' ? created.payment_intent : null,
        })
        .where(eq(orders.id, orderId));
    } catch {
      /* Ne pas renvoyer une page HTML 500 : le paiement Stripe est déjà créé ; sync webhook possible. */
    }
  }

  return new Response(JSON.stringify({ ok: true, id: created.id, url: created.url, reference }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
}

export const POST: APIRoute = async (ctx) => {
  try {
    return await runCheckoutSessionPost(ctx);
  } catch (err) {
    return new Response(
      JSON.stringify({
        error: (err as Error).message ?? 'Erreur serveur inattendue (voir logs Vercel / variables STRIPE_SECRET_KEY).',
      }),
      { status: 500, headers: { 'Content-Type': 'application/json' } },
    );
  }
};
