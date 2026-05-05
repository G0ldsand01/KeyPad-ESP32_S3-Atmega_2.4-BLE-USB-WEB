# Dossier `.vercel` (local + CI)

## `output/`

Généré par `astro build` (adapter Vercel) + patch runtime. **Il est ignoré par Git**
(`.gitignore` : `.vercel/output/`).

Avant chaque `pnpm run build`, le script `prebuild` **efface** ce dossier pour éviter :

- un `vercel deploy --prebuilt` qui repousse d’anciens fichiers ;
- des incohérences après `git pull` si le build n’a pas tout régénéré.

## Lien projet (`project.json`)

Si tu déploies depuis la machine avec la CLI :

```bash
pnpm dlx vercel link
```

Ça crée / met à jour `.vercel/project.json` (souvent **non commité**).  
Les déploiements déclenchés par **push GitHub → Vercel** n’ont pas besoin de ce fichier.

## En prod les changements « ne partent pas »

1. Vérifie sur Vercel que le dernier **Deployment** correspond bien au commit poussé.
2. N’utilise pas `vercel deploy --prebuilt` avec un vieux `.vercel/output`.
3. Les en-têtes `Cache-Control: no-store` sur `/scripts/` sont dans `vercel.json`.
