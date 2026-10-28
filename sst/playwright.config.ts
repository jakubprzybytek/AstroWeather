import { defineConfig, devices } from "@playwright/test";
import { Resource } from "sst";

// E2E tests against a deployed stage: npx sst shell --stage <stage> -- npm run test:e2e
// They sign in as the test user, which prod rejects, and must never run there.
if (Resource.App.stage === "prod") throw new Error("E2E tests never run against prod");

export const STORAGE_STATE = "tests/e2e/.auth/user.json"; // gitignored

export default defineConfig({
  testDir: "./tests/e2e",
  timeout: 60_000,
  retries: process.env.CI ? 1 : 0,
  reporter: [["list"], ["html", { open: "never" }]],
  use: { baseURL: process.env.BASE_URL ?? Resource.AstroWeb.url, trace: "retain-on-failure" },
  projects: [
    { name: "setup", testMatch: /auth\.setup\.ts/, use: { trace: "off" } },
    { name: "chromium", use: { ...devices["Desktop Chrome"], storageState: STORAGE_STATE }, dependencies: ["setup"] }
  ]
});
