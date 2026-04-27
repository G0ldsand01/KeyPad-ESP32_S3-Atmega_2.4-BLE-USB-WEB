import { c as createComponent, g as renderComponent, r as renderTemplate, d as createAstro, m as maybeRenderHead } from '../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../chunks/Layout_BslENFpV.mjs';
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
    return Astro2.redirect("/login?callbackUrl=/dashboard/settings");
  }
  const title = "Param\xE8tres | FlexPad";
  const isAdmin = session.user.role === "admin";
  return renderTemplate`${renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "user", "title": "Param\xE8tres du compte", "subtitle": "Informations li\xE9es \xE0 votre session FlexPad.", "userName": session.user.name ?? session.user.email ?? "Utilisateur", "userEmail": session.user.email ?? "", "isAdmin": isAdmin }, { "default": async ($$result3) => renderTemplate` <div class="dashboard-card reveal visible"> <h2 class="dashboard-card__title">Profil</h2> <ul class="dashboard-meta"> <li><span>ID</span> <code>${session.user.id}</code></li> <li><span>Nom</span> ${session.user.name ?? "\u2014"}</li> <li><span>Courriel</span> ${session.user.email}</li> <li><span>Rôle</span> ${session.user.role === "admin" ? "Administrateur" : "Client"}</li> </ul> <p class="dashboard-note">
La modification du mot de passe ou du courriel pourrait être ajoutée ici (flux sécurisé dédié).
</p> </div> ` })} </div> ` })}`;
}, "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/settings/index.astro", void 0);

const $$file = "C:/Users/Mathieu/OneDrive - Cegep Gerald-Godin/Cegep Gerald-Godin/Session_6/Projet_Finale/Numpad/Projet_Final/src/pages/dashboard/settings/index.astro";
const $$url = "/dashboard/settings";

const _page = /*#__PURE__*/Object.freeze(/*#__PURE__*/Object.defineProperty({
  __proto__: null,
  default: $$Index,
  file: $$file,
  prerender,
  url: $$url
}, Symbol.toStringTag, { value: 'Module' }));

const page = () => _page;

export { page };
