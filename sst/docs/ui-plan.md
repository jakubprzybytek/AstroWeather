# UI Stack Plan

Plan for extending the existing SST app (currently a single API endpoint) with a web UI written in
TypeScript and React.

## 1. What exists today

- **App**: `astroweather-api` (SST v3, `home: "aws"`), defined in `sst/sst.config.ts`.
- **Infrastructure**: one `sst.aws.ApiGatewayV2` component named `AstroApi`.
- **Endpoint**: `GET /astro/{configId}` → `packages/functions/src/astro.handler`.
- **Behaviour of the endpoint** (`packages/functions/src/astro.ts`):
  - `configId` is looked up in a hardcoded `LOCATIONS` map (`krakow-home`, `wroclaw`),
    each entry holding `latitude`, `longitude`, `timezone`.
  - Unknown / missing id → `404` with `{ "message": "Configuration not found" }`.
  - Known id → `200` with:
    ```json
    {
      "configId": "krakow-home",
      "timezone": "Europe/Warsaw",
      "sun":  { "rise": "ISO|null", "set": "ISO|null" },
      "moon": { "rise": "ISO|null", "set": "ISO|null",
                "alwaysUp": false, "alwaysDown": false }
    }
    ```
  - Times are computed for *today* (`new Date()`) with `suncalc`
    (`getTimes`, `getMoonTimes`); invalid dates are serialised as `null`.
- **Tests**: Vitest with two projects (`unit`, `integration`), see `docs/testing.md`.
  Integration tests resolve the API URL through `Resource.AstroApi.url`.

## 2. Goal

A minimal single-page app that lets the user:

1. Pick a configuration from a dropdown (`Kraków`, `Sharm El Sheikh`).
2. Press **Submit**.
3. See the returned sun/moon rise and set times, plus loading and error states.

## 3. Technology choices

| Concern | Choice | Rationale |
|---|---|---|
| Framework | React 19 + TypeScript | Requested; matches repo's TS-only stack. |
| Bundler / dev server | Vite (`react-ts` template) | Standard React+TS scaffolding, fast, first-class SST support. |
| Component & styling library | **MUI (Material UI) v7** + Emotion | Simple, extremely widely used, ships ready-made `Select`, `Button`, `Card`, `Alert`, `CircularProgress` — no custom CSS needed. |
| Hosting | `sst.aws.StaticSite` | Native SST component; S3 + CloudFront, links the API URL at build time. |
| Tests | Vitest (already in repo) + React Testing Library + jsdom | Reuses the existing runner and `vitest.config.ts` projects setup. |

Scaffolding is done with `npm create vite@latest` rather than hand-written files, and
dependencies are added with `npm install` — no manual edits to `package.json`.

## 4. Repository layout

```
sst/
├── sst.config.ts                  # + StaticSite component, api linked
├── vitest.config.ts               # + "web" project (jsdom environment)
├── packages/
│   ├── functions/                 # unchanged (except CORS + new location)
│   └── web/                       # NEW - Vite + React + TS app
│       ├── index.html
│       ├── package.json
│       ├── tsconfig.json
│       ├── vite.config.ts
│       └── src/
│           ├── main.tsx           # React root + MUI CssBaseline/ThemeProvider
│           ├── App.tsx            # page: dropdown + submit + results
│           ├── api.ts             # fetchAstro(configId) typed client
│           ├── types.ts           # AstroResponse type (mirrors handler output)
│           ├── locations.ts       # dropdown options (id → display label)
│           └── components/
│               ├── ConfigSelect.tsx
│               └── AstroResults.tsx
└── docs/
    ├── architecture.md            # + UI section
    ├── testing.md                 # + web test project
    └── ui-plan.md                 # this document
```

## 5. Backend changes required

The UI is a browser client on a different origin than the API, so two small backend
changes are needed:

1. **CORS** — configure `cors` on the `ApiGatewayV2` component in `sst.config.ts`
   (allow `GET`, allow the site origin; `*` is acceptable for this read-only public
   endpoint at this stage).
2. **New location** — add `sharm-el-sheikh`
   (lat `27.9158`, lon `34.3300`, timezone `Africa/Cairo`) to the `LOCATIONS` map in
   `packages/functions/src/astro.ts`, so the second dropdown option resolves.
   Note the existing Kraków entry's id is `krakow-home` — the UI must send that exact id.

No response-shape changes: the UI consumes the payload as-is.

## 6. Infrastructure changes (`sst.config.ts`)

- Keep the existing `AstroApi` component and route.
- Add a `sst.aws.StaticSite` named e.g. `AstroWeb`:
  - `path`: `packages/web`
  - `build`: command `npm run build`, output `dist`
  - `environment`: expose the API URL as `VITE_API_URL` (from `api.url`)
- Extend the stack outputs with the site URL alongside `apiUrl`.
- SST regenerates `sst-env.d.ts` — commit the updated file.

## 7. UI behaviour and states

- **Dropdown**: MUI `Select` with options built from `locations.ts`:
  `{ id: "krakow-home", label: "Kraków" }`, `{ id: "sharm-el-sheikh", label: "Sharm El Sheikh" }`.
  Adding a location later means one entry in this array plus one in the Lambda map.
- **Submit button**: disabled while no selection is made and while a request is in flight.
- **States**:
  - *idle* — nothing below the form;
  - *loading* — `CircularProgress`, button disabled;
  - *success* — `Card` / `Table` listing sun rise, sun set, moon rise, moon set;
    the location's timezone is shown, times are formatted with
    `Intl.DateTimeFormat` using the `timezone` field from the response;
    `null` times render as `—`, and `alwaysUp` / `alwaysDown` render as an explanatory note;
  - *error* — MUI `Alert severity="error"`, using the API's `message` for `404`
    and a generic message for network/5xx failures.
- Requests are made with `fetch` against `import.meta.env.VITE_API_URL`; a `.env` /
  `.env.local` fallback documented in the README lets developers point at `sst dev`.
- Stale-response guard: ignore a response if the user changed selection and submitted again
  (track the in-flight request, e.g. via an incrementing request id or `AbortController`).

## 8. Testing plan

- Add a third Vitest project `web` in `vitest.config.ts`:
  include `packages/web/src/**/*.test.tsx`, `environment: "jsdom"`, React plugin.
- Component tests (React Testing Library, `fetch` stubbed with `vi.fn`):
  - renders both dropdown options;
  - submit triggers a call to `/astro/{selectedId}` with the expected id;
  - success renders the formatted times;
  - `404` renders the error alert;
  - network failure renders the generic error alert.
- Unit test for the time-formatting helper (including `null` and always-up/down cases).
- Extend the existing integration test with a case for `sharm-el-sheikh`.
- New scripts: `npm run test:web`; `test:all` picks the new project up automatically.

## 9. Documentation updates

- `docs/architecture.md`: add a "Web UI" section (StaticSite, S3 + CloudFront, data flow
  browser → API Gateway → Lambda) and update the project structure.
- `docs/testing.md`: document the `web` Vitest project and the RTL/jsdom setup.
- Short `packages/web/README.md` with local dev instructions (`sst dev`, `VITE_API_URL`).

## 10. Implementation steps

1. Scaffold `packages/web` with `npm create vite@latest packages/web -- --template react-ts`.
2. Install runtime deps: `@mui/material @emotion/react @emotion/styled @mui/icons-material`.
3. Install dev deps: `@testing-library/react @testing-library/jest-dom jsdom @vitejs/plugin-react`.
4. Add `types.ts`, `locations.ts`, `api.ts`; implement `ConfigSelect`, `AstroResults`, `App`.
5. Add `sharm-el-sheikh` to the Lambda `LOCATIONS` map and enable CORS on the API.
6. Add the `StaticSite` component and the site URL output in `sst.config.ts`.
7. Add the `web` Vitest project and the component/unit tests; run `npm run test:all`.
8. Verify locally with `sst dev`, then `sst deploy` to a personal stage.
9. Update `docs/architecture.md`, `docs/testing.md`, and add the web README.

## 11. Out of scope (possible follow-ups)

- Date picker (endpoint always computes for "today").
- Storing configurations in DynamoDB (see `architecture.md` Phase 2).
- Authentication, custom domain, i18n, dark-mode toggle.
