import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead, F as Fragment, e as addAttribute } from '../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../chunks/Layout_e5wlI47w.mjs';
import { $ as $$DashboardChrome } from '../../chunks/DashboardChrome_CnSmYUvn.mjs';
import { g as getSession } from '../../chunks/server_BNGpEuMe.mjs';
import { eq, desc } from 'drizzle-orm';
import { d as db, o as orders } from '../../chunks/index_Dzm_i-4A.mjs';
import { f as formatCadFromCents } from '../../chunks/orders_C_9vI6qd.mjs';
export { renderers } from '../../renderers.mjs';

const $$Astro = createAstro();
const prerender = false;
const $$Index = createComponent(async ($$result, $$props, $$slots) => {
  const Astro2 = $$result.createAstro($$Astro, $$props, $$slots);
  Astro2.self = $$Index;
  const session = await getSession(Astro2.request);
  if (!session?.user) {
    return Astro2.redirect("/login?callbackUrl=/dashboard/orders");
  }
  const dbEnabled = Boolean("postgresql://neondb_owner:npg_VgXcfq5tns6e@ep-lively-fog-amu68i1l-pooler.c-5.us-east-1.aws.neon.tech/neondb?channel_binding=require&sslmode=require");
  const userOrders = dbEnabled ? await db.select().from(orders).where(eq(orders.userId, session.user.id)).orderBy(desc(orders.createdAt)) : [];
  function parseItems(json) {
    try {
      const o = JSON.parse(json);
      return Array.isArray(o.items) ? o.items : [];
    } catch {
      return [];
    }
  }
  function statusLabel(s) {
    const m = {
      confirmed: "Confirmée",
      processing: "En traitement",
      shipped: "Expédiée",
      cancelled: "Annulée"
    };
    return m[s] ?? s;
  }
  const title = "Mes commandes | FlexPad";
  const isAdmin = session.user.role === "admin";
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "user", "title": "Mes commandes", "subtitle": "Commandes enregistrées sur ce compte (checkout effectué en étant connecté).", "userName": session.user.name ?? session.user.email ?? "Utilisateur", "userEmail": session.user.email ?? "", "isAdmin": isAdmin }, { "default": async ($$result3) => renderTemplate`${userOrders.length === 0 ? renderTemplate`<div class="dashboard-card reveal visible dash-empty"> <p class="dash-empty__icon" aria-hidden="true">📦</p> <h2 class="dashboard-card__title">${dbEnabled ? "Aucune commande" : "Historique désactivé"}</h2> <p class="dashboard-note"> ${dbEnabled ? renderTemplate`${renderComponent($$result3, "Fragment", Fragment, {}, { "default": async ($$result4) => renderTemplate`
Passez une commande depuis la page <a href="/checkout/">Commander</a> en étant connecté pour la voir ici.
` })}` : renderTemplate`${renderComponent($$result3, "Fragment", Fragment, {}, { "default": async ($$result4) => renderTemplate`
La base de données est désactivée pour le déploiement Vercel: les commandes ne sont pas enregistrées.
` })}`} </p> <a href="/product/" class="btn-primary">Voir le produit</a> </div>` : renderTemplate`<div class="dash-order-list"> ${userOrders.map((order) => {
    const items = parseItems(order.cartJson);
    const qty = items.reduce((n, i) => n + (i.quantity ?? 0), 0);
    return renderTemplate`<article class="dashboard-card reveal visible dash-order-card"> <header class="dash-order-card__head"> <div> <h2 class="dashboard-card__title dash-order-card__ref">${order.reference}</h2> <p class="dash-order-card__date"> ${order.createdAt ? new Date(order.createdAt).toLocaleDateString("fr-CA", {
      year: "numeric",
      month: "long",
      day: "numeric"
    }) : ""} </p> ${(order.stripePaymentIntentId || order.stripeCheckoutSessionId) && renderTemplate`<p class="dashboard-note" style="margin: 0.5rem 0 0;"> ${order.stripePaymentIntentId && renderTemplate`${renderComponent($$result3, "Fragment", Fragment, {}, { "default": async ($$result4) => renderTemplate`
Paiement Stripe: <code>${order.stripePaymentIntentId}</code> ` })}`} ${order.stripePaymentIntentId && order.stripeCheckoutSessionId ? " · " : ""} ${order.stripeCheckoutSessionId && renderTemplate`${renderComponent($$result3, "Fragment", Fragment, {}, { "default": async ($$result4) => renderTemplate`
Session: <code>${order.stripeCheckoutSessionId}</code> ` })}`} </p>`} </div> <div class="dash-order-card__totals"> <span${addAttribute(`dash-status dash-status--${order.status}`, "class")}>${statusLabel(order.status)}</span> <strong class="dash-order-card__price">${formatCadFromCents(order.totalCents)}</strong> <span class="dash-order-card__qty">${qty} article${qty !== 1 ? "s" : ""}</span> </div> </header> ${items.length > 0 && renderTemplate`<ul class="dash-order-items"> ${items.map((item, idx) => renderTemplate`<li> <span>${item.name ?? `Article ${idx + 1}`}</span> <span>Qté ${item.quantity ?? 0}</span> <span> ${formatCadFromCents(Math.round((item.price ?? 0) * (item.quantity ?? 0) * 100))} </span> </li>`)} </ul>`} </article>`;
  })} </div>`}` })} </div> ` })}`;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/orders/index.astro", void 0);
const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/orders/index.astro";
const $$url = "/dashboard/orders";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
