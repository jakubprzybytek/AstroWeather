import { Resource } from "sst";
import type { TestProject } from "vitest/node";

declare module "vitest" {
  export interface ProvidedContext {
    apiUrl: string;
  }
}

// Test workers on Windows receive environment variable names upper-cased, which hides
// SST_RESOURCE_* links from `Resource`, so resolve the URL here and hand it over.
export function setup(project: TestProject) {
  console.log("Integration test environment:");
  console.log(`  API URL: ${Resource.AstroApi.url}`);
  project.provide("apiUrl", Resource.AstroApi.url);
}
