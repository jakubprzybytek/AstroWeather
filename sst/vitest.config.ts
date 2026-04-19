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
          testTimeout: 15000
        }
      },

    ]
  }
});
