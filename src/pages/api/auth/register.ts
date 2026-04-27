import type { APIRoute } from 'astro';
import { randomUUID } from 'node:crypto';
import bcrypt from 'bcryptjs';
import { eq } from 'drizzle-orm';
import { db } from '../../../db';
import { users } from '../../../db/schema';

export const prerender = false;

export const POST: APIRoute = async ({ request }) => {
  // Déploiement Vercel sans DB: inscription indisponible.
  const url = process.env.DATABASE_URL?.trim();
  if (!url || !/^postgres(ql)?:\/\//i.test(url)) {
    return new Response(JSON.stringify({ error: 'Inscription indisponible (base désactivée).' }), {
      status: 503,
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

  const name = typeof (body as { name?: unknown }).name === 'string' ? (body as { name: string }).name.trim() : '';
  const emailRaw = (body as { email?: unknown }).email;
  const passwordRaw = (body as { password?: unknown }).password;
  const email = typeof emailRaw === 'string' ? emailRaw.toLowerCase().trim() : '';
  const password = typeof passwordRaw === 'string' ? passwordRaw : '';

  if (!email || !email.includes('@') || password.length < 6) {
    return new Response(JSON.stringify({ error: 'Paramètres invalides' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const [existing] = await db.select({ id: users.id }).from(users).where(eq(users.email, email)).limit(1);
  if (existing?.id) {
    return new Response(JSON.stringify({ error: 'Ce courriel est déjà utilisé.' }), {
      status: 409,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const id = randomUUID();
  const passwordHash = await bcrypt.hash(password, 10);

  await db.insert(users).values({
    id,
    email,
    name: name || null,
    passwordHash,
    role: 'user',
  });

  return new Response(JSON.stringify({ ok: true }), {
    status: 200,
    headers: { 'Content-Type': 'application/json' },
  });
};

