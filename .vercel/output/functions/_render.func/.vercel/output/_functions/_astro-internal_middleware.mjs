import { d as defineMiddleware, s as sequence } from './chunks/index_B-QEPDv4.mjs';
import { g as getSession } from './chunks/server_BNGpEuMe.mjs';
import './chunks/astro-designed-error-pages_ZwubNhbX.mjs';
import 'cookie';

const onRequest$1 = defineMiddleware(async (context, next) => {
  const path = context.url.pathname;
  if (path.startsWith("/dashboard/admin")) {
    const session = await getSession(context.request);
    if (!session?.user) {
      const callback = encodeURIComponent(path);
      return context.redirect(`/login?callbackUrl=${callback}`);
    }
    if (session.user.role !== "admin") {
      return context.redirect("/dashboard");
    }
    return next();
  }
  if (path.startsWith("/dashboard")) {
    const session = await getSession(context.request);
    if (!session?.user) {
      const callback = encodeURIComponent(path);
      return context.redirect(`/login?callbackUrl=${callback}`);
    }
  }
  return next();
});

const onRequest = sequence(
	
	onRequest$1
	
);

export { onRequest };
