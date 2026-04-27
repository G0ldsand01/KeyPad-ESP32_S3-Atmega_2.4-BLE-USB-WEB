import type { APIRoute } from 'astro';
import { eq } from 'drizzle-orm';
import { getSession } from 'auth-astro/server';
import { db } from '../../../db';
import { orders } from '../../../db/schema';
import { parseOrderStatus } from '../../../lib/orders';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  const url = process.env.DATABASE_URL?.trim();
  if (!url || !/^postgres(ql)?:\/\//i.test(url)) {
    return new Response(JSON.stringify({ error: 'Base de données désactivée (démo Vercel)' }), {
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

  let body: { orderId?: string; status?: string };
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'JSON invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const orderId = typeof body.orderId === 'string' ? body.orderId : '';
  const status = parseOrderStatus(body.status);
  if (!orderId || !status) {
    return new Response(JSON.stringify({ error: 'Paramètres invalides' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const updated = await db
    .update(orders)
    .set({ status })
    .where(eq(orders.id, orderId))
    .returning({ id: orders.id });
  if (updated.length === 0) {
    return new Response(JSON.stringify({ error: 'Commande introuvable' }), {
      status: 404,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  return new Response(JSON.stringify({ ok: true }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};
