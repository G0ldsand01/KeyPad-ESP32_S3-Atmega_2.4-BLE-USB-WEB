import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead } from '../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../chunks/Layout_BslENFpV.mjs';
import { $ as $$DashboardChrome } from '../../chunks/DashboardChrome_CnSmYUvn.mjs';
import { g as getSession } from '../../chunks/server_CatkvZha.mjs';
import { d as db, o as orders, u as users } from '../../chunks/index_CRES2LJX.mjs';
import { f as formatCadFromCents } from '../../chunks/orders_C_9vI6qd.mjs';
import { sum, count, desc } from 'drizzle-orm';
export { renderers } from '../../renderers.mjs';

const $$Astro = createAstro();
const prerender = false;
const $$Index = createComponent(async ($$result, $$props, $$slots) => {
  const Astro2 = $$result.createAstro($$Astro, $$props, $$slots);
  Astro2.self = $$Index;
  const session = await getSession(Astro2.request);
  if (!session?.user || session.user.role !== "admin") {
    return Astro2.redirect("/dashboard");
  }
  const dbEnabled = Boolean("postgresql://neondb_owner:npg_VgXcfq5tns6e@ep-lively-fog-amu68i1l-pooler.c-5.us-east-1.aws.neon.tech/neondb?channel_binding=require&sslmode=require");
  const [orderAgg] = dbEnabled ? await db.select({
    n: count(),
    revenue: sum(orders.totalCents)
  }).from(orders) : [{ n: 0, revenue: 0 }];
  const [userAgg] = dbEnabled ? await db.select({ n: count() }).from(users) : [{ n: 0 }];
  const latest = dbEnabled ? await db.select().from(orders).orderBy(desc(orders.createdAt)).limit(8) : [];
  const title = "Administration | FlexPad";
  const nOrders = Number(orderAgg?.n ?? 0);
  const revenueCents = Number(orderAgg?.revenue ?? 0);
  const nUsers = Number(userAgg?.n ?? 0);
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "admin", "title": "Vue d’ensemble", "subtitle": dbEnabled ? "Indicateurs issus de la base locale (commandes enregistrées + comptes)." : "Base désactivée (démo Vercel) — métriques non disponibles.", "userName": session.user.name ?? session.user.email ?? "Admin", "userEmail": session.user.email ?? "", "isAdmin": true }, { "default": async ($$result3) => renderTemplate` <div class="admin-metrics"> <div class="admin-metric reveal visible"> <span class="admin-metric__label">Commandes</span> <strong class="admin-metric__value">${nOrders}</strong> </div> <div class="admin-metric reveal visible"> <span class="admin-metric__label">Chiffre (total enregistré)</span> <strong class="admin-metric__value">${formatCadFromCents(revenueCents)}</strong> </div> <div class="admin-metric reveal visible"> <span class="admin-metric__label">Comptes utilisateurs</span> <strong class="admin-metric__value">${nUsers}</strong> </div> </div> <div class="dashboard-card reveal visible dash-recent"> <h2 class="dashboard-card__title">Dernières commandes (tous clients)</h2> ${latest.length === 0 ? renderTemplate`<p class="dashboard-note">Aucune commande en base.</p>` : renderTemplate`<ul class="dash-recent-list admin-recent-orders"> ${latest.map((o) => renderTemplate`<li> <a href="/dashboard/admin/orders"> <strong>${o.reference}</strong> <span class="dash-recent-meta"> ${formatCadFromCents(o.totalCents)} · ${o.status} </span> </a> </li>`)} </ul>`} </div> ` })} </div> ` })}`;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/index.astro", void 0);
const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/index.astro";
const $$url = "/dashboard/admin";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
