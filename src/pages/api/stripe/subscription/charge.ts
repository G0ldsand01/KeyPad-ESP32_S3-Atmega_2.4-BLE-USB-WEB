import type { APIRoute } from 'astro';
import { getSession } from 'auth-astro/server';
import { stripePost } from '../../../../lib/stripe-http';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  const session = await getSession(request);
  if (!session?.user || session.user.role !== 'admin') {
    return new Response(JSON.stringify({ error: 'Interdit' }), {
      status: 403,
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

  const stripeAccountId =
    typeof (body as { stripeAccountId?: unknown }).stripeAccountId === 'string'
      ? (body as { stripeAccountId: string }).stripeAccountId
      : process.env.STRIPE_CONNECTED_ACCOUNT_ID?.trim();
  const priceId =
    typeof (body as { priceId?: unknown }).priceId === 'string'
      ? (body as { priceId: string }).priceId
      : '';

  if (!stripeAccountId || !stripeAccountId.startsWith('acct_')) {
    return new Response(JSON.stringify({ error: 'stripeAccountId invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }
  if (!priceId) {
    return new Response(JSON.stringify({ error: 'priceId manquant' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  // Blueprint: /v1/setup_intents (payment_method_types: stripe_balance, confirm: true, customer_account: acct_...)
  const setup = await stripePost<{ id: string; payment_method?: string }>(
    '/v1/setup_intents',
    {
      payment_method_types: ['stripe_balance'],
      confirm: true,
      customer_account: stripeAccountId,
      usage: 'off_session',
      payment_method_data: { type: 'stripe_balance' },
    },
  );

  // Blueprint: /v1/subscriptions (customer_account + default_payment_method + items[0][price])
  const sub = await stripePost<{ id: string }>(
    '/v1/subscriptions',
    {
      customer_account: stripeAccountId,
      default_payment_method: setup.payment_method,
      items: [{ price: priceId, quantity: 1 }],
      payment_settings: { payment_method_types: ['stripe_balance'] },
    },
  );

  return new Response(JSON.stringify({ ok: true, setupIntentId: setup.id, subscriptionId: sub.id }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

