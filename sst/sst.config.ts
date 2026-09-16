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
    const forecastData = new sst.aws.Dynamo("ForecastData", {
      fields: {
        pk: "string",
        sk: "string"
      },
      primaryIndex: { hashKey: "pk", rangeKey: "sk" },
      ttl: "expireAt"
    });

    const api = new sst.aws.ApiGatewayV2("AstroApi", {
      cors: {
        allowMethods: ["GET", "POST"],
        allowOrigins: ["*"]
      }
    });

    api.route("GET /configurations", "packages/functions/src/configurations-handler.handler");
    api.route("GET /astro/{configurationId}", "packages/functions/src/astro.handler");
    api.route("POST /tools/clearoutside", "packages/functions/src/clearoutside.handler");

    new sst.aws.CronV2("ClearOutsideIngestion", {
      schedule: "rate(6 hours)",
        retries: 0,
      function: {
        handler: "packages/functions/src/jobs/clearoutside-weather.handler",
        link: [forecastData],
        timeout: "2 minutes"
      }
    });

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
      siteUrl: web.url,
      forecastDataTableName: forecastData.name
    };
  }
});
