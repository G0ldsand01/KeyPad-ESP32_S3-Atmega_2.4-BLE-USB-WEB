import type { APIRoute } from 'astro';

export const prerender = false;

export const GET: APIRoute = async () => {
  const secret = (import.meta.env.STRIPE_SECRET_KEY || process.env.STRIPE_SECRET_KEY || '').trim();
  const publishable = (import.meta.env.STRIPE_PUBLISHABLE_KEY || process.env.STRIPE_PUBLISHABLE_KEY || '').trim();
  const hasSecret = Boolean(secret);
  const hasPublishable = Boolean(publishable);

  return new Response(
    JSON.stringify({
      ok: hasSecret && hasPublishable,
      hasSecret,
      hasPublishable,
      // ne jamais retourner la clé complète
      secretPrefix: secret ? secret.slice(0, 7) : '',
    }),
    { status: 200, headers: { 'Content-Type': 'application/json' } },
  );
};

