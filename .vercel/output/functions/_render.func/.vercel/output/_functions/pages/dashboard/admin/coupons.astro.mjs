import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead } from '../../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../../chunks/Layout_QyV7CE-J.mjs';
import { $ as $$DashboardChrome } from '../../../chunks/DashboardChrome_CnSmYUvn.mjs';
import { $ as $$AdminPlaceholder } from '../../../chunks/AdminPlaceholder_BZ8aSNJ4.mjs';
import { g as getSession } from '../../../chunks/server_CatkvZha.mjs';
export { renderers } from '../../../renderers.mjs';

const $$Astro = createAstro();
const prerender = false;
const $$Index = createComponent(async ($$result, $$props, $$slots) => {
  const Astro2 = $$result.createAstro($$Astro, $$props, $$slots);
  Astro2.self = $$Index;
  const session = await getSession(Astro2.request);
  if (!session?.user || session.user.role !== "admin") return Astro2.redirect("/dashboard");
  const title = "Admin \u2014 Coupons | FlexPad";
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "admin", "title": "Coupons", "subtitle": "Codes promo et remises.", "userName": session.user.name ?? session.user.email ?? "Admin", "userEmail": session.user.email ?? "", "isAdmin": true }, { "default": async ($$result3) => renderTemplate` ${renderComponent($$result3, "AdminPlaceholder", $$AdminPlaceholder, { "title": "Codes promotionnels", "body": "Cr\xE9ation et suivi de coupons (pourcentage, montant fixe, dates de validit\xE9) \u2014 \xE0 connecter au tunnel de commande." })} ` })} </div> ` })}`;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/coupons/index.astro", void 0);

const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/coupons/index.astro";
const $$url = "/dashboard/admin/coupons";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
