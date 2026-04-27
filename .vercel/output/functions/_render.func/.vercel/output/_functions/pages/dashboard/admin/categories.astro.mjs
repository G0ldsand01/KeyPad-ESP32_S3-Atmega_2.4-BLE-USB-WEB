import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead } from '../../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../../chunks/Layout_B3T-4WXt.mjs';
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
  const title = "Admin \u2014 Cat\xE9gories | FlexPad";
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "admin", "title": "Cat\xE9gories", "subtitle": "Organisation du catalogue.", "userName": session.user.name ?? session.user.email ?? "Admin", "userEmail": session.user.email ?? "", "isAdmin": true }, { "default": async ($$result3) => renderTemplate` ${renderComponent($$result3, "AdminPlaceholder", $$AdminPlaceholder, { "title": "Gestion des cat\xE9gories", "body": "FlexPad est un produit unique pour la d\xE9mo ; une vraie boutique pourrait classer accessoires, pi\xE8ces, etc." })} ` })} </div> ` })}`;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/categories/index.astro", void 0);

const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/admin/categories/index.astro";
const $$url = "/dashboard/admin/categories";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
