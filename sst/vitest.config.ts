import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    projects: [
      {
        test: {
          name: "unit",
          include: ["packages/**/src/**/*.test.ts"]
        }
      },
      {
        test: {
          name: "integration",
          include: ["packages/**/tests/integration/**/*.test.ts"],
          globalSetup: ["packages/functions/tests/integration/globalSetup.ts"],
          testTimeout: 15000
        }
      },

    ]
  }
});
