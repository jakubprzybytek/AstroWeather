# AstroWeather web UI

## Local development

From `sst/`, run `npm run dev` and let SST provide the API URL and the Cognito
user pool and app client IDs. To use a separately running API, create
`packages/web/.env.local` with:

```text
VITE_API_URL=http://localhost:3000
VITE_USER_POOL_ID=eu-west-1_IVai0KEAA
VITE_USER_POOL_CLIENT_ID=2i29cn3m973qqh3re94fj7oejr
```

The app opens on a sign-in form; any account of the shared Albedo user pool
works, or create one there. See `docs/architecture.md#access-control`.

Run the production build with `npm run build` from this directory.

Bootstrap theme variables can be customized in `src/styles/_variables.scss`; the
file is loaded before Bootstrap in `src/styles/main.scss`.
