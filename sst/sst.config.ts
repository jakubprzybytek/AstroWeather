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
      removal: input?.stage === "prod" ? "retain" : "remove",
      home: "aws"
    };
  },
  async run() {
    const domain = $app.stage === "prod"
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
      transform: {
        stage: {
          defaultRouteSettings: {
            throttlingBurstLimit: 1,
            throttlingRateLimit: 1
          }
        }
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
    api.route("GET /astro/{configurationId}", {
      handler: "packages/functions/src/astro.handler",
      link: [forecastData]
    });
    api.route("POST /tools/clearoutside", "packages/functions/src/clearoutside.handler");
    api.route("POST /tools/gfz-hp60", "packages/functions/src/tools/gfz-hp60.handler");
    api.route("POST /tools/noaa-kp", "packages/functions/src/tools/noaa-kp.handler");
    api.route("POST /tools/noaa-outlook", "packages/functions/src/tools/noaa-outlook.handler");
    api.route("POST /tools/ovation", "packages/functions/src/tools/ovation.handler");

    new sst.aws.CronV2("ClearOutsideIngestion", {
      // 00:00, 06:00, 12:00 and 18:00 local. The HostController refreshes at
      // 10 minutes past these hours, so each fetch picks up fresh weather.
      schedule: "cron(0 0/6 * * ? *)",
      timezone: "Europe/Warsaw",
        retries: 0,
      function: {
        handler: "packages/functions/src/jobs/clearoutside-weather.handler",
        link: [forecastData],
        timeout: "2 minutes"
      }
    });

    new sst.aws.CronV2("AuroraIngestion", {
      // With the weather, so the device's :10 refresh finds both fresh; see
      // docs/aurora-forecast-supplier.md#fetch-cadence.
      schedule: "cron(0 0/6 * * ? *)",
      timezone: "Europe/Warsaw",
      retries: 0,
      function: {
        handler: "packages/functions/src/jobs/aurora-forecast.handler",
        link: [forecastData],
        timeout: "2 minutes"
      }
    });

    new sst.aws.CronV2("AuroraNowcast", {
      // Every ten minutes, off the hour; does nothing unless a location's
      // night is a flagged storm night and it is dark there.
      schedule: "cron(3/10 * * * ? *)",
      retries: 0,
      function: {
        handler: "packages/functions/src/jobs/aurora-nowcast.handler",
        link: [forecastData],
        timeout: "2 minutes",
        memory: "512 MB"
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
      apiUrl: `https://${domain.api}`,
      siteUrl: web.url,
      forecastDataTableName: forecastData.name
    };
  }
});
