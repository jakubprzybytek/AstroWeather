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

### Integration Tests

Integration tests verify end-to-end behaviour of Lambda handlers by calling them via HTTP. The same test files run against both the local dev environment and a live deployed stage — the only difference is the base URL passed through the `API_URL` environment variable.

**Location**: `packages/<pkg>/tests/integration/**/*.test.ts` (separate folder, not co-located)

**Run locally** (requires `sst dev` to already be running):
```bash
# API_URL defaults to http://localhost:3000 when not set
npm run test:integration
```

**Run against a deployed stage**:
```bash
API_URL=https://<deployed-url> npm run test:integration
```

### All Tests

Runs both suites in a single command:
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
│           └── integration/                  # integration tests (local or stage)
│               └── astro.integration.test.ts
```

## Vitest Projects

`vitest.config.ts` declares two named projects. The `--project` flag selects which suite to run:

| npm script | Vitest project | Include pattern | Target environment |
|---|---|---|---|
| `npm run test` | `unit` | `packages/**/src/**/*.test.ts` | local (no network) |
| `npm run test:integration` | `integration` | `packages/**/tests/integration/**/*.test.ts` | local `sst dev` or deployed stage via `API_URL` |
| `npm run test:all` | _(all)_ | all patterns above | — |

## CI Usage

| Pipeline stage | Command | Notes |
|---|---|---|
| Pull Request | `npm run test` | Unit tests only; no external deps required |
| Post-deploy (staging) | `API_URL=$STAGE_URL npm run test:integration` | Smoke test after `sst deploy` to staging |
| Post-deploy (production) | `API_URL=$PROD_URL npm run test:integration` | Smoke test after production deploy |

`API_URL` is injected as a CI secret / environment variable. When running locally without setting `API_URL`, tests fall back to `http://localhost:3000`.

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

## Writing Integration Tests

Integration tests call the handler via HTTP and read the base URL from `API_URL` (defaulting to localhost):

```typescript
import { describe, it, expect } from "vitest";

const BASE_URL = process.env.API_URL ?? "http://localhost:3000";

describe("GET /astro/:configId", () => {
  it("returns 200 for a known configId", async () => {
    const res = await fetch(`${BASE_URL}/astro/krakow-home`);
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body).toHaveProperty("sun.rise");
  });
});
```

## SunCalc Mocking Strategies

There are four approaches for mocking `suncalc` in unit tests, with different trade-offs:

### 1. `vi.mock()` — whole-module replacement (recommended)

```typescript
vi.mock("suncalc", () => ({
  default: { getTimes: vi.fn(), getMoonTimes: vi.fn() }
}));
```

Vitest hoists `vi.mock()` calls before any imports, so the stub is in place before the module under test loads. This is the simplest and most robust approach when you want full control over every call. Per-test return values are set with `mockReturnValue` / `mockResolvedValue`.

**Pros**: complete isolation; no real computation runs; works even if `suncalc` is not installed.  
**Cons**: you must explicitly stub every method you use; easy to forget a method and get `undefined`.

### 2. `vi.spyOn()` — wrap individual methods

```typescript
import SunCalc from "suncalc";
const spy = vi.spyOn(SunCalc, "getTimes").mockReturnValue({ ... });
```

Wraps a single method with a spy while leaving the rest of the module intact. The real implementation can be restored with `spy.mockRestore()`.

**Pros**: surgical — only the targeted method is replaced; real implementations of other methods still run.  
**Cons**: `suncalc` must be importable; harder to guarantee full isolation; spy state leaks between tests if not restored.

### 3. Manual mock in `__mocks__/`

Create `packages/functions/src/__mocks__/suncalc.ts` (adjacent to `node_modules` or next to the source):

```typescript
// __mocks__/suncalc.ts
export default {
  getTimes: vi.fn(),
  getMoonTimes: vi.fn()
};
```

Vitest picks this up automatically when `vi.mock("suncalc")` is called without a factory. Keeping mock logic in a dedicated file makes it reusable across multiple test files without copy-paste.

**Pros**: single source of truth for the mock; easy to share across test files.  
**Cons**: slightly more file ceremony; factory-less `vi.mock()` call can be surprising to newcomers.

### 4. Dependency injection — no mock framework needed

Refactor the handler to accept a `sunCalc` parameter (or a factory) instead of importing it directly:

```typescript
// astro.ts
export function createHandler(sunCalc = SunCalc) {
  return async (event) => { /* use sunCalc.getTimes(...) */ };
}
export const handler = createHandler();
```

In tests, pass a plain stub object:

```typescript
const fakeSunCalc = { getTimes: vi.fn(), getMoonTimes: vi.fn() };
const handler = createHandler(fakeSunCalc);
```

**Pros**: zero coupling to Vitest mocking machinery; dependency is explicit in the API; trivially testable.  
**Cons**: changes the production API surface; requires refactoring the handler signature.

