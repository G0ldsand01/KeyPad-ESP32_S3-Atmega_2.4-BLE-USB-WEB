import { renderers } from './renderers.mjs';
import { c as createExports } from './chunks/entrypoint_CTtjM27d.mjs';
import { manifest } from './manifest_BiyqS4I7.mjs';

const _page0 = () => import('./pages/_image.astro.mjs');
const _page1 = () => import('./pages/about.astro.mjs');
const _page2 = () => import('./pages/api/admin/order-status.astro.mjs');
const _page3 = () => import('./pages/api/auth/register.astro.mjs');
const _page4 = () => import('./pages/api/auth/_---auth_.astro.mjs');
const _page5 = () => import('./pages/api/orders.astro.mjs');
const _page6 = () => import('./pages/checkout.astro.mjs');
const _page7 = () => import('./pages/config.astro.mjs');
const _page8 = () => import('./pages/contact.astro.mjs');
const _page9 = () => import('./pages/dashboard/admin/analytics.astro.mjs');
const _page10 = () => import('./pages/dashboard/admin/categories.astro.mjs');
const _page11 = () => import('./pages/dashboard/admin/coupons.astro.mjs');
const _page12 = () => import('./pages/dashboard/admin/email-templates.astro.mjs');
const _page13 = () => import('./pages/dashboard/admin/logs.astro.mjs');
const _page14 = () => import('./pages/dashboard/admin/notifications.astro.mjs');
const _page15 = () => import('./pages/dashboard/admin/orders.astro.mjs');
const _page16 = () => import('./pages/dashboard/admin/products.astro.mjs');
const _page17 = () => import('./pages/dashboard/admin/reports.astro.mjs');
const _page18 = () => import('./pages/dashboard/admin/reviews.astro.mjs');
const _page19 = () => import('./pages/dashboard/admin/settings.astro.mjs');
const _page20 = () => import('./pages/dashboard/admin/users.astro.mjs');
const _page21 = () => import('./pages/dashboard/admin.astro.mjs');
const _page22 = () => import('./pages/dashboard/orders.astro.mjs');
const _page23 = () => import('./pages/dashboard/settings.astro.mjs');
const _page24 = () => import('./pages/dashboard/wishlist.astro.mjs');
const _page25 = () => import('./pages/dashboard.astro.mjs');
const _page26 = () => import('./pages/features.astro.mjs');
const _page27 = () => import('./pages/login.astro.mjs');
const _page28 = () => import('./pages/panier.astro.mjs');
const _page29 = () => import('./pages/product.astro.mjs');
const _page30 = () => import('./pages/index.astro.mjs');

const pageMap = new Map([
    ["node_modules/.pnpm/astro@4.16.19_@types+node@2_c1441c87a5410574ede951559bdd7524/node_modules/astro/dist/assets/endpoint/generic.js", _page0],
    ["src/pages/about/index.astro", _page1],
    ["src/pages/api/admin/order-status.ts", _page2],
    ["src/pages/api/auth/register.ts", _page3],
    ["src/pages/api/auth/[...auth].ts", _page4],
    ["src/pages/api/orders/index.ts", _page5],
    ["src/pages/checkout/index.astro", _page6],
    ["src/pages/config/index.astro", _page7],
    ["src/pages/contact/index.astro", _page8],
    ["src/pages/dashboard/admin/analytics/index.astro", _page9],
    ["src/pages/dashboard/admin/categories/index.astro", _page10],
    ["src/pages/dashboard/admin/coupons/index.astro", _page11],
    ["src/pages/dashboard/admin/email-templates/index.astro", _page12],
    ["src/pages/dashboard/admin/logs/index.astro", _page13],
    ["src/pages/dashboard/admin/notifications/index.astro", _page14],
    ["src/pages/dashboard/admin/orders/index.astro", _page15],
    ["src/pages/dashboard/admin/products/index.astro", _page16],
    ["src/pages/dashboard/admin/reports/index.astro", _page17],
    ["src/pages/dashboard/admin/reviews/index.astro", _page18],
    ["src/pages/dashboard/admin/settings/index.astro", _page19],
    ["src/pages/dashboard/admin/users/index.astro", _page20],
    ["src/pages/dashboard/admin/index.astro", _page21],
    ["src/pages/dashboard/orders/index.astro", _page22],
    ["src/pages/dashboard/settings/index.astro", _page23],
    ["src/pages/dashboard/wishlist/index.astro", _page24],
    ["src/pages/dashboard/index.astro", _page25],
    ["src/pages/features/index.astro", _page26],
    ["src/pages/login.astro", _page27],
    ["src/pages/panier/index.astro", _page28],
    ["src/pages/product/index.astro", _page29],
    ["src/pages/index.astro", _page30]
]);
const serverIslandMap = new Map();
const _manifest = Object.assign(manifest, {
    pageMap,
    serverIslandMap,
    renderers,
    middleware: () => import('./_astro-internal_middleware.mjs')
});
const _args = {
    "middlewareSecret": "9fbba9d7-22f5-488b-bf9a-6232e47c8c5c",
    "skewProtection": false
};
const _exports = createExports(_manifest, _args);
const __astrojsSsrVirtualEntry = _exports.default;

export { __astrojsSsrVirtualEntry as default, pageMap };
