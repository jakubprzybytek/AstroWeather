// Stage-specific behaviour lives here, so sst.config.ts checks flags instead of stage names.
// What each stage is for: docs/development.md.

const stage = $app.stage;

export const isProd = stage === "prod";

// prod lives at the bare domain, other stages under a prefix.
export const domain = isProd
  ? { web: "astroweather.albedoonline.com", api: "api.astroweather.albedoonline.com" }
  : { web: `${stage}.astroweather.albedoonline.com`, api: `api.${stage}.astroweather.albedoonline.com` };

// Ingestion (ClearOutside, GFZ, NOAA) runs only where the device reads: prod. dev and int serve
// whatever their table holds, and the forecast degrades to "?" without data.
export const scheduleEnabled = !$dev && isProd;

// Cognito groups the signed-in routes accept (the pool is shared, and anyone may register).
// The test user's group only off prod.
export const TEST_USER = "astroweather-test@albedoonline.com";
export const accessGroups = isProd ? ["astroweather"] : ["astroweather", "astroweather-test"];
