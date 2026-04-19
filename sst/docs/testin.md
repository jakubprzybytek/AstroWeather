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

**Location**: `packages/<pkg>/src/**/*.test.ts` (co-located with source files)

**Run**:
```bash
npm run test
```

### Integration Tests (Local)

Integration tests verify end-to-end behaviour of a Lambda handler against a locally running SST dev environment (`sst dev`). They invoke the handler as HTTP calls against the local API URL, which is read from an environment variable or SST dev outputs.

**Location**: `packages/<pkg>/tests/integration/**/*.test.ts`

**Run** (requires `sst dev` to already be running):
```bash
npm run test:integration
```

### Integration Tests (Live)

Integration tests run against a fully deployed AWS stage. These are used for post-deploy smoke testing in CI. The target API URL is provided via the `API_URL` environment variable.

> **Not yet implemented.** The test structure and scripts are in place for when this is needed.

**Location**: `packages/<pkg>/tests/integration-live/**/*.test.ts`

**Run**:
```bash
API_URL=https://<deployed-url> npm run test:integration:live
```

### All Tests

Runs all three suites in a single command:
```bash
npm run test:all
```

## Project Structure

```
sst/
├── vitest.config.ts                     # Vitest projects config
├── packages/
│   └── functions/
│       └── src/
│           ├── astro.ts
│           └── astro.test.ts            # unit tests (co-located)
│       └── tests/
│           ├── integration/             # local SST dev tests
│           └── integration-live/        # live AWS stage tests
```

## Vitest Projects

`vitest.config.ts` declares three named projects that map to the three test types. The `--project` flag selects which suite to run:

| npm script | Vitest project | Include pattern |
|---|---|---|
| `npm run test` | `unit` | `packages/**/src/**/*.test.ts` |
| `npm run test:integration` | `integration` | `packages/**/tests/integration/**/*.test.ts` |
| `npm run test:integration:live` | `integration-live` | `packages/**/tests/integration-live/**/*.test.ts` |
| `npm run test:all` | _(all)_ | all patterns above |

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
