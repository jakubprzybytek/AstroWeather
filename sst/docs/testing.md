# Testing Approach

## Tech Stack

| Concern | Tool |
|---|---|
| Test runner | [Vitest](https://vitest.dev/) v4 |
| Language | TypeScript (native ESM, no transpilation step) |
| Mocking | Vitest built-ins (`vi.mock`, `vi.fn`, `vi.spyOn`) |

Vitest was chosen because it supports native ESM and TypeScript out of the box, shares the same API surface as Jest, and integrates well with a monorepo project structure through its **projects** feature.

## Test Types

### Unit Tests

Unit tests exercise individual functions and modules in isolation. External dependencies (e.g. `suncalc`) are mocked so tests remain fast and deterministic.

**Location**: co-located with source files — `packages/<pkg>/src/**/*.test.ts`

**Run**:
```bash
npm run test
```

### Integration Tests (Local)

Integration tests verify end-to-end behaviour of Lambda handlers against a **locally running SST dev environment** (`sst dev`). They call the handler via HTTP against the local API URL.

**Location**: `packages/<pkg>/tests/integration/**/*.test.ts` (separate folder, not co-located)

**Run** (requires `sst dev` to already be running):
```bash
npm run test:integration
```

### Integration Tests (Live)

Integration tests run against a **fully deployed AWS stage**. Used for post-deploy smoke testing in CI. The target API URL is provided via the `API_URL` environment variable.

> **Not yet implemented.** The test structure and scripts are in place for when this is needed.

**Location**: `packages/<pkg>/tests/integration-int/**/*.test.ts` (separate folder, not co-located)

**Run**:
```bash
API_URL=https://<deployed-url> npm run test:integration:int
```

### All Tests

Runs all three suites in a single command:
```bash
npm run test:all
```

## Project Structure

```
sst/
├── vitest.config.ts                          # Vitest projects config
├── packages/
│   └── functions/
│       └── src/
│           ├── astro.ts
│           └── astro.test.ts                 # unit test (co-located with source)
│       └── tests/
│           ├── integration/                  # local SST dev tests
│           │   └── astro.integration.test.ts
│           └── integration-int/              # live AWS stage tests
│               └── astro.integration.test.ts
```

## Vitest Projects

`vitest.config.ts` declares three named projects that map to the three test types. The `--project` flag selects which suite to run:

| npm script | Vitest project | Include pattern | Target environment |
|---|---|---|---|
| `npm run test` | `unit` | `packages/**/src/**/*.test.ts` | local (no network) |
| `npm run test:integration` | `integration` | `packages/**/tests/integration/**/*.test.ts` | local `sst dev` |
| `npm run test:integration:int` | `integration-int` | `packages/**/tests/integration-int/**/*.test.ts` | live AWS stage |
| `npm run test:all` | _(all)_ | all patterns above | — |

## CI Usage

In CI pipelines, each test type is run at a different stage:

| Pipeline stage | Command | Notes |
|---|---|---|
| Pull Request | `npm run test` | Unit tests always run; no external deps required |
| Post-deploy (staging) | `API_URL=$STAGE_URL npm run test:integration:int` | Runs after a successful `sst deploy` to staging; `API_URL` is passed as a secret/env var |
| Post-deploy (production) | `API_URL=$PROD_URL npm run test:integration:int` | Smoke test after production deploy |

Local integration tests (`npm run test:integration`) are **not** run in CI — they require a local `sst dev` session and are intended for developer use only.

## Writing Unit Tests

Mock external libraries at the top of the test file, then import the module under test:

```typescript
import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("suncalc", () => ({
  default: {
    getTimes: vi.fn(),
    getMoonTimes: vi.fn()
  }
}));

import SunCalc from "suncalc";
import { handler } from "./astro.js";

describe("handler", () => {
  beforeEach(() => vi.clearAllMocks());

  it("returns 404 for unknown configId", async () => {
    const result = await handler({ pathParameters: { configId: "unknown" } });
    expect(result.statusCode).toBe(404);
  });
});
```

## Writing Integration Tests (Local)

Integration tests call the handler via HTTP. The local API base URL is expected in the `LOCAL_API_URL` environment variable (set by the dev environment or a `.env.test` file):

```typescript
import { describe, it, expect } from "vitest";

const BASE_URL = process.env.LOCAL_API_URL ?? "http://localhost:3000";

describe("GET /astro/:configId (local)", () => {
  it("returns 200 for a known configId", async () => {
    const res = await fetch(`${BASE_URL}/astro/krakow-home`);
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body).toHaveProperty("sun.rise");
  });
});
```

## Writing Integration Tests (Live)

Live integration tests are structured the same way as local ones, but read `API_URL` instead:

```typescript
import { describe, it, expect } from "vitest";

const BASE_URL = process.env.API_URL;
if (!BASE_URL) throw new Error("API_URL env var is required for live integration tests");

describe("GET /astro/:configId (live)", () => {
  it("returns 200 for a known configId", async () => {
    const res = await fetch(`${BASE_URL}/astro/krakow-home`);
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body).toHaveProperty("sun.rise");
  });
});
```

