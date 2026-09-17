/// <reference path="./.sst/platform/config.d.ts" />

const hostedZoneId = "Z041419132FCBY6ZLLXL2";

function route53Dns() {
  const dns = sst.aws.dns();

  return {
    ...dns,
    createAlias(...args: Parameters<typeof dns.createAlias>) {
      const [namePrefix, record, opts] = args;

      return ["A", "AAAA"].map((type) =>
        $output(
          new aws.route53.Record(
            `${namePrefix}${type}Record`,
            {
              zoneId: hostedZoneId,
              type,
              name: record.name,
              aliases: [{
                name: record.aliasName,
                zoneId: record.aliasZone,
                evaluateTargetHealth: true
              }]
            },
            opts
          )
        )
      );
    },
    createRecord(...args: Parameters<typeof dns.createRecord>) {
      const [namePrefix, record, opts] = args;

      return $output(record).apply((resolved) => {
        if (!resolved.name || !resolved.type || !resolved.value) return undefined;

        return new aws.route53.Record(
          `${namePrefix}Record`,
          {
            zoneId: hostedZoneId,
            type: resolved.type,
            name: resolved.name,
            ttl: 60,
            records: [
              resolved.priority === undefined
                ? resolved.value
                : `${resolved.priority} ${resolved.value}`
            ]
          },
          opts
        );
      }) as ReturnType<typeof dns.createRecord>;
    }
  };
}

export default $config({
  app(input) {
    return {
      name: "astroweather-api",
      removal: input?.stage === "production" ? "retain" : "remove",
      home: "aws"
    };
  },
  async run() {
    const domain = $app.stage === "production"
      ? {
          web: "astroweather.albedoonline.com",
          api: "api.astroweather.albedoonline.com"
        }
      : {
          web: `${$app.stage}.astroweather.albedoonline.com`,
          api: `api.${$app.stage}.astroweather.albedoonline.com`
        };

    const forecastData = new sst.aws.Dynamo("ForecastData", {
      fields: {
        pk: "string",
        sk: "string"
      },
      primaryIndex: { hashKey: "pk", rangeKey: "sk" },
      ttl: "expireAt"
    });

    const api = new sst.aws.ApiGatewayV2("AstroApi", {
      domain: {
        name: domain.api,
        dns: route53Dns()
      },
      cors: {
        allowMethods: ["GET", "POST"],
        allowOrigins: [
          `https://${domain.web}`,
          "http://localhost:5173",
          "http://localhost:3000"
        ]
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
      domain: {
        name: domain.web,
        dns: route53Dns()
      },
      build: {
        command: "npm run build",
        output: "dist"
      },
      environment: {
        VITE_API_URL: `https://${domain.api}`
      }
    });

    return {
      apiUrl: api.url,
      siteUrl: web.url,
      forecastDataTableName: forecastData.name
    };
  }
});
