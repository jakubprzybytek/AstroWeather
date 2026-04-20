/// <reference path="./.sst/platform/config.d.ts" />

export default $config({
  app(input) {
    return {
      name: "astroweather-api",
      removal: input?.stage === "production" ? "retain" : "remove",
      home: "aws"
    };
  },
  async run() {
    const api = new sst.aws.ApiGatewayV2("AstroApi");

    api.route("GET /astro/{configId}", "packages/functions/src/astro.handler");

    return {
      apiUrl: api.url
    };
  }
});
