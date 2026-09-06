# AstroWeather web UI

## Local development

From `sst/`, run `npm run dev` and let SST provide the API URL. To use a separately
running API, create `packages/web/.env.local` with:

```text
VITE_API_URL=http://localhost:3000
```

Run the production build with `npm run build` from this directory.

Bootstrap theme variables can be customized in `src/styles/_variables.scss`; the
file is loaded before Bootstrap in `src/styles/main.scss`.
