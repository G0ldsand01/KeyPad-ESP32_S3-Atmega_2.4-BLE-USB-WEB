/// <reference path="../.astro/types.d.ts" />

interface ImportMetaEnv {
  readonly AUTH_SECRET?: string;
  readonly AUTH_TRUST_HOST?: string;
  readonly DATABASE_URL?: string;
  readonly STRIPE_SECRET_KEY?: string;
  readonly STRIPE_PUBLISHABLE_KEY?: string;
  /** Secret de signature webhook (Dashboard Stripe → Developers → Webhooks) */
  readonly STRIPE_WEBHOOK_SECRET?: string;
  /** Ex: "CAD" */
  readonly STRIPE_CURRENCY?: string;
  /** Ex: "CA" */
  readonly STRIPE_CONNECTED_ACCOUNT_COUNTRY?: string;
  /**
   * Optionnel: si tu as déjà un compte connecté (acct_...)
   * sinon l’API peut en créer un via /api/stripe/connect/account
   */
  readonly STRIPE_CONNECTED_ACCOUNT_ID?: string;
  /** Frais appliqués sur les paiements en mode Connect (en cents) */
  readonly STRIPE_APPLICATION_FEE_CENTS?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}