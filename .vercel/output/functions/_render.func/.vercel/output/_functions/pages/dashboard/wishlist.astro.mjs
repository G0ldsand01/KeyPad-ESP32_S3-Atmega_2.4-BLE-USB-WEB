import { c as createComponent, r as renderTemplate, g as renderComponent, d as createAstro, m as maybeRenderHead } from '../../chunks/astro/server_DYDqy7ws.mjs';
import { $ as $$Layout } from '../../chunks/Layout_QyV7CE-J.mjs';
import { $ as $$DashboardChrome } from '../../chunks/DashboardChrome_CnSmYUvn.mjs';
import { g as getSession } from '../../chunks/server_CatkvZha.mjs';
export { renderers } from '../../renderers.mjs';

var __freeze = Object.freeze;
var __defProp = Object.defineProperty;
var __template = (cooked, raw) => __freeze(__defProp(cooked, "raw", { value: __freeze(cooked.slice()) }));
var _a;
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
  return renderTemplate(_a || (_a = __template(["", ` <script>
  (function () {
    var KEY = 'flexpad_wishlist';

    function whenDocumentReady(cb) {
      if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', cb, { once: true });
      } else {
        queueMicrotask(cb);
      }
    }

    function readList() {
      try {
        var raw = localStorage.getItem(KEY);
        var data = raw ? JSON.parse(raw) : [];
        return Array.isArray(data) ? data : [];
      } catch (e) {
        return [];
      }
    }

    function render() {
      var root = document.getElementById('wishlist-root');
      var emptyTpl = document.getElementById('wishlist-empty-tpl');
      var listTpl = document.getElementById('wishlist-list-tpl');
      if (!root) return;
      var items = readList();
      root.innerHTML = '';
      if (items.length === 0) {
        var node = emptyTpl && emptyTpl.content ? emptyTpl.content.cloneNode(true) : null;
        if (node) root.appendChild(node);
        return;
      }
      var frag = listTpl && listTpl.content ? listTpl.content.cloneNode(true) : null;
      if (!frag) return;
      var ul = frag.querySelector('#wishlist-items');
      items.forEach(function (entry) {
        var li = document.createElement('li');
        li.className = 'wishlist-item';
        var name = typeof entry.name === 'string' ? entry.name : 'Produit';
        var id = typeof entry.id === 'string' ? entry.id : '';
        li.innerHTML =
          '<span class="wishlist-item__name">' +
          name +
          '</span><button type="button" class="btn-secondary btn-sm wishlist-remove" data-id="' +
          id +
          '">Retirer</button>';
        if (ul) ul.appendChild(li);
      });
      root.appendChild(frag);
      root.querySelectorAll('.wishlist-remove').forEach(function (btn) {
        btn.addEventListener('click', function () {
          var id = btn.getAttribute('data-id');
          var next = readList().filter(function (x) {
            return x.id !== id;
          });
          localStorage.setItem(KEY, JSON.stringify(next));
          render();
        });
      });
    }

    document.addEventListener('astro:page-load', render);
    whenDocumentReady(render);
  })();
<\/script>`])), renderComponent($$result, "Layout", $$Layout, { "title": title, "site": true }, { "default": async ($$result2) => renderTemplate` ${maybeRenderHead()}<div class="page-content dashboard-page dashboard-page--chrome"> ${renderComponent($$result2, "DashboardChrome", $$DashboardChrome, { "variant": "user", "title": "Liste de souhaits", "subtitle": "Produits sauvegard\xE9s dans ce navigateur (localStorage).", "userName": session.user.name ?? session.user.email ?? "Utilisateur", "userEmail": session.user.email ?? "", "isAdmin": isAdmin }, { "default": async ($$result3) => renderTemplate` <div class="dashboard-card reveal visible"> <div id="wishlist-root" class="wishlist-root"> <p class="dashboard-note">Chargement…</p> </div> <template id="wishlist-empty-tpl"> <p class="dash-empty__icon" aria-hidden="true">♡</p> <h2 class="dashboard-card__title">Liste vide</h2> <p class="dashboard-note">
Ajoutez FlexPad à votre liste depuis la <a href="/product/">fiche produit</a>.
</p> <a href="/product/" class="btn-primary">Voir FlexPad</a> </template> <template id="wishlist-list-tpl"> <ul class="wishlist-items" id="wishlist-items"></ul> <p class="dashboard-note wishlist-hint">
Données locales à ce navigateur — elles ne sont pas synchronisées avec le serveur.
</p> </template> </div> ` })} </div> ` }));
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
