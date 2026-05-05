import crypto from 'node:crypto';

type StripeRequestOptions = {
  stripeAccount?: string;
};

type StripeErrorBody = {
  error?: {
    type?: string;
    message?: string;
    code?: string;
    param?: string;
  };
};

function requireStripeSecretKey(): string {
  const key = (import.meta.env.STRIPE_SECRET_KEY || process.env.STRIPE_SECRET_KEY || '').trim();
  if (!key) throw new Error('STRIPE_SECRET_KEY manquant dans .env');
  return key;
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function encodeStripeForm(
  value: unknown,
  prefix = '',
  out: Array<[string, string]> = [],
): Array<[string, string]> {
  if (value === undefined) return out;
  if (value === null) {
    out.push([prefix, '']);
    return out;
  }
  if (Array.isArray(value)) {
    value.forEach((v, i) => encodeStripeForm(v, `${prefix}[${i}]`, out));
    return out;
  }
  if (isPlainObject(value)) {
    for (const [k, v] of Object.entries(value)) {
      encodeStripeForm(v, prefix ? `${prefix}[${k}]` : k, out);
    }
    return out;
  }
  out.push([prefix, String(value)]);
  return out;
}

export async function stripePost<T = unknown>(
  path: string,
  params: Record<string, unknown>,
  opts: StripeRequestOptions = {},
): Promise<T> {
  const pairs = encodeStripeForm(params);
  const body = new URLSearchParams(pairs).toString();

  const res = await fetch(`https://api.stripe.com${path}`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${requireStripeSecretKey()}`,
      'Content-Type': 'application/x-www-form-urlencoded',
      ...(opts.stripeAccount ? { 'Stripe-Account': opts.stripeAccount } : null),
    },
    body,
  });

  const json = (await res.json().catch(() => ({}))) as unknown;
  if (!res.ok) {
    const err = json as StripeErrorBody;
    const msg = err?.error?.message || `Erreur Stripe (${res.status})`;
    throw new Error(msg);
  }
  return json as T;
}

/**
 * Vérifie la signature Stripe sans dépendance externe.
 * Stripe-Signature: t=...,v1=...,v0=...
 */
export function verifyStripeWebhookSignature(rawBody: string, signatureHeader: string, secret: string): void {
  const parts = signatureHeader.split(',').map((s) => s.trim());
  const tPart = parts.find((p) => p.startsWith('t='));
  const v1Parts = parts.filter((p) => p.startsWith('v1='));
  const t = tPart ? tPart.slice(2) : '';
  if (!t || v1Parts.length === 0) {
    throw new Error('Signature Stripe invalide (en-tête manquant)');
  }

  const signedPayload = `${t}.${rawBody}`;
  const expected = crypto.createHmac('sha256', secret).update(signedPayload, 'utf8').digest('hex');
  const ok = v1Parts.some((p) => {
    const sig = p.slice(3);
    try {
      return crypto.timingSafeEqual(Buffer.from(sig), Buffer.from(expected));
    } catch {
      return false;
    }
  });
  if (!ok) throw new Error('Signature Stripe invalide');
}

