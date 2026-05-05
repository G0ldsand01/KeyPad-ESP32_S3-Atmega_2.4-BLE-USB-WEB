import type { APIRoute } from 'astro';
import { getSession } from 'auth-astro/server';
import { stripePost } from '../../../../lib/stripe-http';

export const prerender = false;

export const POST: APIRoute = async ({ request, url }) => {
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
    body = {};
  }

  const accountIdRaw = (body as { stripeAccountId?: unknown }).stripeAccountId;
  const stripeAccountId =
    typeof accountIdRaw === 'string' && accountIdRaw.startsWith('acct_')
      ? accountIdRaw
      : process.env.STRIPE_CONNECTED_ACCOUNT_ID?.trim();

  if (!stripeAccountId) {
    return new Response(JSON.stringify({ error: 'stripeAccountId manquant' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const refreshUrl = `${url.origin}/dashboard`;
  const returnUrl = `${url.origin}/dashboard`;

  // Blueprint: /v2/core/account_links
  const link = await stripePost<{ url: string }>(
    '/v2/core/account_links',
    {
      account: stripeAccountId,
      use_case: {
        type: 'account_onboarding',
        account_onboarding: {
          configurations: ['merchant', 'customer'],
          refresh_url: refreshUrl,
          return_url: returnUrl,
        },
      },
    },
  );

  return new Response(JSON.stringify({ ok: true, url: link.url }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

