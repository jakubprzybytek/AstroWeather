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
    const api = new sst.aws.ApiGatewayV2("AstroApi", {
      cors: {
        allowMethods: ["GET"],
        allowOrigins: ["*"]
      }
    });

    api.route("GET /astro/{configId}", "packages/functions/src/astro.handler");

    const web = new sst.aws.StaticSite("AstroWeb", {
      path: "packages/web",
      build: {
        command: "npm run build",
        output: "dist"
      },
      environment: {
        VITE_API_URL: api.url
      }
    });

    return {
      apiUrl: api.url,
      siteUrl: web.url
    };
  }
});
