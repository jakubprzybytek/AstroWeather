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
| Component library | **React-Bootstrap v2** | Accessible, typed React components (`Form.Select`, `Button`, `Card`, `Table`, `Alert`, `Spinner`) with no jQuery and no CSS-in-JS runtime. |
| Styling | **Bootstrap 5 Sass** (`bootstrap` + `sass-embedded`) | Lightweight compared to MUI/Emotion; SCSS is Bootstrap's native customisation path — override variables, then `@use "bootstrap/scss/bootstrap"`. Vite compiles SCSS out of the box once `sass-embedded` is installed. |
| Hosting | `sst.aws.StaticSite` | Native SST component; S3 + CloudFront, links the API URL at build time. |
| Tests | Vitest (already in repo) + React Testing Library + jsdom | Reuses the existing runner and `vitest.config.ts` projects setup. |

Scaffolding is done with `npm create vite@latest` rather than hand-written files, and
dependencies are added with `npm install` — no manual edits to `package.json`.

Alternatives considered: MUI (heavier, pulls in the Emotion CSS-in-JS runtime, CSS-in-JS
rather than Sass), Bulma (Sass-native but ships no React components), and unstyled
primitives such as Radix (most flexibility, most hand-written CSS). Bootstrap was chosen
as the best balance of "lightweight + SCSS-first + ready-made React components".

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
│           ├── main.tsx           # React root, imports styles/main.scss
│           ├── App.tsx            # page: dropdown + submit + results
│           ├── api.ts             # fetchAstro(configId) typed client
│           ├── types.ts           # AstroResponse type (mirrors handler output)
│           ├── locations.ts       # dropdown options (id → display label)
│           ├── styles/
│           │   ├── _variables.scss  # Bootstrap variable overrides (colors, fonts)
│           │   └── main.scss        # overrides + `@use "bootstrap/scss/bootstrap"`
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

- **Layout**: React-Bootstrap `Container` + `Row`/`Col`, page inside a `Card`.
- **Dropdown**: React-Bootstrap `Form.Select` with options built from `locations.ts`:
  `{ id: "krakow-home", label: "Kraków" }`, `{ id: "sharm-el-sheikh", label: "Sharm El Sheikh" }`.
  Adding a location later means one entry in this array plus one in the Lambda map.
- **Submit button**: React-Bootstrap `Button type="submit"`, disabled while no selection is
  made and while a request is in flight; the form submits via `onSubmit` so Enter works too.
- **States**:
  - *idle* — nothing below the form;
  - *loading* — `Spinner` (inside the button), button disabled;
  - *success* — `Card` with a `Table` listing sun rise, sun set, moon rise, moon set;
    the location's timezone is shown, times are formatted with
    `Intl.DateTimeFormat` using the `timezone` field from the response;
    `null` times render as `—`, and `alwaysUp` / `alwaysDown` render as an explanatory note;
  - *error* — `Alert variant="danger"`, using the API's `message` for `404`
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
- Short `packages/web/README.md` with local dev instructions (`sst dev`, `VITE_API_URL`)
  and how to theme the app through the Bootstrap SCSS variable overrides.

## 10. Implementation steps

1. Scaffold `packages/web` with `npm create vite@latest packages/web -- --template react-ts`.
2. Install runtime deps: `bootstrap react-bootstrap`.
3. Install dev deps: `sass-embedded @testing-library/react @testing-library/user-event @testing-library/jest-dom jsdom @vitejs/plugin-react`.
4. Add `src/styles/_variables.scss` + `src/styles/main.scss` (variable overrides before
   `@use "bootstrap/scss/bootstrap"`) and import `main.scss` once from `main.tsx`.
5. Add `types.ts`, `locations.ts`, `api.ts`; implement `ConfigSelect`, `AstroResults`, `App`.
6. Add `sharm-el-sheikh` to the Lambda `LOCATIONS` map and enable CORS on the API.
7. Add the `StaticSite` component and the site URL output in `sst.config.ts`.
8. Add the `web` Vitest project and the component/unit tests; run `npm run test:all`.
9. Verify locally with `sst dev`, then `sst deploy` to a personal stage.
10. Update `docs/architecture.md`, `docs/testing.md`, and add the web README.

## 11. Out of scope (possible follow-ups)

- Date picker (endpoint always computes for "today").
- Storing configurations in DynamoDB (see `architecture.md` Phase 2).
- Authentication, custom domain, i18n, dark-mode toggle.
