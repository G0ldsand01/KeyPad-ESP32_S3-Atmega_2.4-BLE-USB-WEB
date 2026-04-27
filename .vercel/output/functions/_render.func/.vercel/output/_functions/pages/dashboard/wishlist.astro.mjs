import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead } from '../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../chunks/Layout_B3T-4WXt.mjs';
import { $ as $$DashboardChrome } from '../../chunks/DashboardChrome_CnSmYUvn.mjs';
import { g as getSession } from '../../chunks/server_CatkvZha.mjs';
export { renderers } from '../../renderers.mjs';

const $$Astro = createAstro();
const prerender = false;
const $$Index = createComponent(async ($$result, $$props, $$slots) => {
  const Astro2 = $$result.createAstro($$Astro, $$props, $$slots);
  Astro2.self = $$Index;
  const session = await getSession(Astro2.request);
  if (!session?.user) {
    return Astro2.redirect("/login?callbackUrl=/dashboard/wishlist");
  }
  const title = "Liste de souhaits | FlexPad";
  const isAdmin = session.user.role === "admin";
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "user", "title": "Liste de souhaits", "subtitle": "Produits sauvegard\xE9s dans ce navigateur (localStorage).", "userName": session.user.name ?? session.user.email ?? "Utilisateur", "userEmail": session.user.email ?? "", "isAdmin": isAdmin }, { "default": async ($$result3) => renderTemplate` <div class="dashboard-card reveal visible"> <div id="wishlist-root" class="wishlist-root"> <p class="dashboard-note">Chargement…</p> </div> <template id="wishlist-empty-tpl"> <p class="dash-empty__icon" aria-hidden="true">♡</p> <h2 class="dashboard-card__title">Liste vide</h2> <p class="dashboard-note">
Ajoutez FlexPad à votre liste depuis la <a href="/product/">fiche produit</a>.
</p> <a href="/product/" class="btn-primary">Voir FlexPad</a> </template> <template id="wishlist-list-tpl"> <ul class="wishlist-items" id="wishlist-items"></ul> <p class="dashboard-note wishlist-hint">
Données locales à ce navigateur — elles ne sont pas synchronisées avec le serveur.
</p> </template> </div> ` })} </div> ` })} `;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/wishlist/index.astro", void 0);

const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/wishlist/index.astro";
const $$url = "/dashboard/wishlist";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
