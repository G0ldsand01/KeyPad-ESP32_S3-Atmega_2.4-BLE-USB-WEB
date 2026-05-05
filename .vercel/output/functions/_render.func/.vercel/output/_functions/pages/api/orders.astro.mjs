import { randomUUID } from 'node:crypto';
import { g as getSession } from '../../chunks/server_BNGpEuMe.mjs';
import { d as db, o as orders } from '../../chunks/index_Dzm_i-4A.mjs';
import { n as normalizeCartPayload, t as taxesFromSubtotalCents } from '../../chunks/orders_C_9vI6qd.mjs';
export { renderers } from '../../renderers.mjs';

const prerender = false;
const POST = async ({ request }) => {
  const session = await getSession(request);
  if (!session?.user?.id) {
    return new Response(JSON.stringify({ error: "Non authentifié" }), {
      status: 401,
      headers: { "Content-Type": "application/json" }
    });
  }
  let body;
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: "JSON invalide" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }
  const cartIn = body?.cart;
  const normalized = normalizeCartPayload(cartIn);
  if (!normalized) {
    return new Response(JSON.stringify({ error: "Panier invalide" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }
  const { tpsCents, tvqCents, totalCents } = taxesFromSubtotalCents(normalized.subtotalCents);
  const refRaw = body.reference;
  const reference = typeof refRaw === "string" && /^FXP-[A-Za-z0-9-]+$/.test(refRaw) ? refRaw : `FXP-${Date.now().toString(36).toUpperCase()}`;
  const shipping = body.shipping;
  const shippingJson = shipping && typeof shipping === "object" ? JSON.stringify(shipping) : null;
  const id = randomUUID();
  const dbUrl = process.env.DATABASE_URL?.trim();
  const dbEnabled = Boolean(dbUrl && /^postgres(ql)?:\/\//i.test(dbUrl));
  if (dbEnabled) {
    const write = db.insert(orders).values({
      id,
      userId: session.user.id,
      reference,
      status: "confirmed",
      cartJson: JSON.stringify(normalized.items),
      shippingJson,
      subtotalCents: normalized.subtotalCents,
      tpsCents,
      tvqCents,
      totalCents
    });
    const timeoutMs = 2500;
    const timeout = new Promise((resolve) => setTimeout(() => resolve(null), timeoutMs));
    try {
      await Promise.race([write, timeout]);
    } catch {
    }
  }
  return new Response(
    JSON.stringify({
      ok: true,
      orderId: id,
      reference,
      subtotalCents: normalized.subtotalCents,
      tpsCents,
      tvqCents,
      totalCents,
      shippingJson,
      dbEnabled
    }),
    {
      status: 200,
      headers: { "Content-Type": "application/json" }
    }
  );
};

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  POST,
  prerender
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
