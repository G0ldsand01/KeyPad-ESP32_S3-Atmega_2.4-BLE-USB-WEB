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

  const currency = (process.env.STRIPE_CURRENCY?.trim() || 'CAD').toLowerCase();

  // Blueprint: /v1/products avec default_price_data
  const product = await stripePost<{ id: string; default_price?: string }>(
    '/v1/products',
    {
      name: 'Platform subscription',
      default_price_data: {
        currency,
        recurring: { interval: 'month' },
        unit_amount: 1000,
      },
    },
  );

  return new Response(JSON.stringify({ ok: true, productId: product.id, defaultPriceId: product.default_price }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

