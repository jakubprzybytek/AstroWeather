/// <reference path="./.sst/platform/config.d.ts" />

const hostedZoneId = "Z041419132FCBY6ZLLXL2";

// The shared Albedo user pool, created and managed outside this app. Each stage
// gets its own app client (public, SRP, no secret). Anyone may register in the
// pool, so the signed-in routes also check the user's group
// (packages/functions/src/auth/user-access.ts, groups in infra/stage.ts).
const userPool = {
  region: "eu-west-1",
  id: "eu-west-1_IVai0KEAA"
};

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
      protect: input?.stage === "prod",
      home: "aws"
    };
  },
  async run() {
    const { accessGroups, domain, isProd, scheduleEnabled, TEST_USER } = await import("./infra/stage");

    // App client: SRP everywhere; plain-password sign-in (the integration tests) only off prod.
    const webClient = new aws.cognito.UserPoolClient("WebClient", {
      name: `astroweather-${$app.stage}-web`,
      userPoolId: userPool.id,
      explicitAuthFlows: [
        "ALLOW_USER_SRP_AUTH",
        "ALLOW_REFRESH_TOKEN_AUTH",
        ...(isProd ? [] : ["ALLOW_USER_PASSWORD_AUTH"])
      ],
      preventUserExistenceErrors: "ENABLED"
    });

    // Test user (docs/development.md): read by the integration and E2E tests through `sst shell`.
    // Not on prod, where its group is rejected.
    if (!isProd) {
      new sst.Secret("TestUserPassword");
      new sst.Linkable("TestUser", { properties: { email: TEST_USER, clientId: webClient.id } });
    }

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
        // Listed by name: a "*" never covers Authorization.
        allowHeaders: ["authorization", "content-type"],
        allowOrigins: [
          `https://${domain.web}`,
          "http://localhost:5173",
          "http://localhost:3000"
        ]
      }
    });

    // Comma-separated, so a new key can be added before the device switches.
    const deviceApiKey = new sst.Secret("DeviceApiKey");
    const deviceAuthorizer = api.addAuthorizer({
      name: "DeviceKey",
      lambda: {
        function: {
          handler: "packages/functions/src/auth/device-key.handler",
          link: [deviceApiKey]
        },
        identitySources: ["$request.querystring.key"],
        response: "simple",
        ttl: "300 seconds"
      }
    });

    const userAuthorizer = api.addAuthorizer({
      name: "Cognito",
      jwt: {
        issuer: `https://cognito-idp.${userPool.region}.amazonaws.com/${userPool.id}`,
        audiences: [webClient.id]
      }
    });
    const userAuth = { auth: { jwt: { authorizer: userAuthorizer.id } } };
    // Signed-in routes: the handler wrapper lets in only ACCESS_GROUPS.
    // The release tag set by continuous-delivery.yaml, sent as the forecast's appVersion and shown in the footer.
    const appVersion = process.env.APP_VERSION ?? "dev";
    const environment = { ACCESS_GROUPS: accessGroups.join(","), APP_VERSION: appVersion };
    const userRoute = (route: string, handler: string, link: sst.aws.FunctionArgs["link"] = []) =>
      api.route(route, { handler, link, environment }, userAuth);

    userRoute("GET /configurations", "packages/functions/src/configurations-handler.handler");
    userRoute("GET /astro/{configurationId}", "packages/functions/src/astro.handler", [forecastData]);
    // The device's copy of the forecast route; the ST67 module cannot send
    // headers, so its key travels in the `key` query parameter.
    api.route("GET /device/astro/{configurationId}", {
      handler: "packages/functions/src/astro.handler",
      link: [forecastData],
      environment: { APP_VERSION: appVersion }
    }, {
      auth: { lambda: deviceAuthorizer.id }
    });
    userRoute("POST /tools/clearoutside", "packages/functions/src/clearoutside.handler");
    userRoute("POST /tools/gfz-hp60", "packages/functions/src/tools/gfz-hp60.handler");
    userRoute("POST /tools/noaa-kp", "packages/functions/src/tools/noaa-kp.handler");
    userRoute("POST /tools/noaa-outlook", "packages/functions/src/tools/noaa-outlook.handler");
    userRoute("POST /tools/ovation", "packages/functions/src/tools/ovation.handler");

    if (scheduleEnabled) {
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
    }

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
        VITE_API_URL: `https://${domain.api}`,
        VITE_USER_POOL_ID: userPool.id,
        VITE_USER_POOL_CLIENT_ID: webClient.id,
        VITE_APP_VERSION: appVersion
      }
    });

    return {
      apiUrl: `https://${domain.api}`,
      siteUrl: web.url,
      forecastDataTableName: forecastData.name
    };
  }
});
