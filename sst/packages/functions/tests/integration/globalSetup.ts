import { Resource } from "sst";

export function setup() {
  console.log("Integration test environment:");
  console.log(`  API URL: ${Resource.AstroApi.url}`);
}
