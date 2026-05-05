/**
 * Supprime `.vercel/output` avant `astro build` pour ne jamais réutiliser
 * un bundle Vercel (prebuilt) périmé ou incomplet après un pull.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, '..');
const out = path.join(root, '.vercel', 'output');
if (fs.existsSync(out)) {
  fs.rmSync(out, { recursive: true, force: true });
  console.log('[clean-vercel-output] .vercel/output supprimé');
}
