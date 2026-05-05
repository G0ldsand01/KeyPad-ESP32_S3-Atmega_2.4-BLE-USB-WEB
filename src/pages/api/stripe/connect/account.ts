import type { APIRoute } from 'astro';
import { randomUUID } from 'node:crypto';
import { getSession } from 'auth-astro/server';
import { db } from '../../../../db';
import { connectedAccounts } from '../../../../db/schema';
import { stripePost } from '../../../../lib/stripe-http';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  const session = await getSession(request);
  if (!session?.user?.id) {
    return new Response(JSON.stringify({ error: 'Non authentifié' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const country =
    (process.env.STRIPE_CONNECTED_ACCOUNT_COUNTRY?.trim() || 'CA').toUpperCase();

  // Blueprint: /v2/core/accounts
  const created = await stripePost<{ id: string }>(
    '/v2/core/accounts',
    {
      display_name: 'Test account',
      contact_email: session.user.email ?? 'testaccount@example.com',
      configuration: {
        merchant: {
          simulate_accept_tos_obo: true,
        },
      },
      include: [
        'configuration.merchant',
        'configuration.recipient',
        'identity',
        'defaults',
        'configuration.customer',
      ],
      identity: {
        country,
        business_details: {
          phone: '0000000000',
        },
      },
      dashboard: 'full',
      defaults: {
        responsibilities: {
          losses_collector: 'stripe',
          fees_collector: 'stripe',
        },
      },
    },
  );

  const dbUrl = process.env.DATABASE_URL?.trim();
  const dbEnabled = Boolean(dbUrl && /^postgres(ql)?:\/\//i.test(dbUrl));
  if (dbEnabled) {
    await db.insert(connectedAccounts).values({
      id: randomUUID(),
      userId: session.user.id,
      stripeAccountId: created.id,
      country,
      onboardingComplete: false,
    });
  }

  return new Response(JSON.stringify({ ok: true, stripeAccountId: created.id, dbEnabled }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

