import { Resource } from "sst";
import type { TestProject } from "vitest/node";

declare module "vitest" {
  export interface ProvidedContext {
    apiUrl: string;
    deviceKey: string;
    testUser: { email: string; password: string; clientId: string };
  }
}

// Test workers on Windows receive environment variable names upper-cased, which hides
// SST_RESOURCE_* links from `Resource`, so resolve the URL and the device key
// (the first of the comma-separated keys) here and hand them over.
export function setup(project: TestProject) {
  if (Resource.App.stage === "prod") throw new Error("Integration tests never run against prod");
  console.log("Integration test environment:");
  console.log(`  API URL: ${Resource.AstroApi.url}`);
  project.provide("apiUrl", Resource.AstroApi.url);
  project.provide("deviceKey", Resource.DeviceApiKey.value.split(",")[0].trim());
  // The test user (docs/development.md); never printed.
  project.provide("testUser", {
    email: Resource.TestUser.email,
    password: Resource.TestUserPassword.value,
    clientId: Resource.TestUser.clientId
  });
}
