/**
 * Proxy sécurisé pour télécharger un asset .bin depuis GitHub Releases.
 * Évite CORS : le firmware est récupéré côté serveur (fetch navigateur → GitHub échoue sinon).
 */
import type { APIRoute } from 'astro';

export const prerender = false;

const MAX_BYTES = 16 * 1024 * 1024; // 16 Mo — limite raisonnable pour un firmware ESP32

function isAllowedGithubBinaryUrl(href: string): URL {
  let u: URL;
  try {
    u = new URL(href);
  } catch {
    throw new Error('URL invalide');
  }
  if (u.protocol !== 'https:') throw new Error('HTTPS uniquement');
  const host = u.hostname.toLowerCase();

  if (host === 'github.com') {
    if (!/^\/[^/]+\/[^/]+\/releases\/download\//.test(u.pathname)) {
      throw new Error(
        'URL GitHub autorisée : …/releases/download/… uniquement (pas une page HTML)',
      );
    }
    return u;
  }

  const okHost =
    host === 'objects.githubusercontent.com' ||
    host === 'release-assets.githubusercontent.com' ||
    host.endsWith('.githubusercontent.com');

  if (!okHost) throw new Error('Hôte non autorisé pour le téléchargement');

  return u;
}

export const POST: APIRoute = async ({ request }) => {
  let body: { url?: string };
  try {
    body = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: 'Corps JSON attendu { "url": "…" }' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  const raw = typeof body.url === 'string' ? body.url.trim() : '';
  if (!raw) {
    return new Response(JSON.stringify({ error: 'Champ url manquant' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  let target: URL;
  try {
    target = isAllowedGithubBinaryUrl(raw);
  } catch (e) {
    const msg = e instanceof Error ? e.message : 'URL refusée';
    return new Response(JSON.stringify({ error: msg }), {
      status: 400,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  try {
    const upstream = await fetch(target.toString(), {
      redirect: 'follow',
      headers: {
        Accept: 'application/octet-stream',
        'User-Agent': 'FlexPad-OTA-Proxy/1.0 (ESP32 firmware)',
      },
    });

    if (!upstream.ok) {
      return new Response(
        JSON.stringify({
          error: `GitHub a répondu ${upstream.status}`,
        }),
        { status: 502, headers: { 'Content-Type': 'application/json' } },
      );
    }

    const len = upstream.headers.get('Content-Length');
    if (len) {
      const n = parseInt(len, 10);
      if (Number.isFinite(n) && n > MAX_BYTES) {
        return new Response(JSON.stringify({ error: 'Fichier trop volumineux' }), {
          status: 413,
          headers: { 'Content-Type': 'application/json' },
        });
      }
    }

    const buf = await upstream.arrayBuffer();
    if (buf.byteLength > MAX_BYTES) {
      return new Response(JSON.stringify({ error: 'Fichier trop volumineux' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json' },
      });
    }

    const filename =
      target.pathname.split('/').pop()?.replace(/[^a-zA-Z0-9._-]/g, '_') || 'firmware.bin';

    return new Response(buf, {
      status: 200,
      headers: {
        'Content-Type': 'application/octet-stream',
        'Content-Length': String(buf.byteLength),
        'Cache-Control': 'no-store',
        'Content-Disposition': `attachment; filename="${filename}"`,
      },
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : 'Erreur réseau';
    return new Response(JSON.stringify({ error: msg }), {
      status: 502,
      headers: { 'Content-Type': 'application/json' },
    });
  }
};
