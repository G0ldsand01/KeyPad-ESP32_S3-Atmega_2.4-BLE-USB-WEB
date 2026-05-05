import type { APIRoute } from 'astro';
import { eq } from 'drizzle-orm';
import { getSession } from 'auth-astro/server';
import { db } from '../../../db';
import { users } from '../../../db/schema';

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

  let body: { email?: string; role?: string };
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'JSON invalide' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const emailRaw = typeof body.email === 'string' ? body.email.trim().toLowerCase() : '';
  const role = body.role === 'admin' || body.role === 'user' ? body.role : null;
  if (!emailRaw || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(emailRaw) || !role) {
    return new Response(JSON.stringify({ error: 'Courriel et rôle (admin|user) requis' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const updated = await db
    .update(users)
    .set({ role })
    .where(eq(users.email, emailRaw))
    .returning({ id: users.id, email: users.email, role: users.role });

  if (updated.length === 0) {
    return new Response(
      JSON.stringify({
        error:
          'Utilisateur introuvable. Le compte doit exister (inscription sur /login ou seed db:seed).',
      }),
      { status: 404, headers: { 'Content-Type': 'application/json' } },
    );
  }

  return new Response(JSON.stringify({ ok: true, user: updated[0] }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};
