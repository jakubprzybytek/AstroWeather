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

    const cloudFrontProvider = new aws.Provider("CloudFrontProvider", {
      region: "us-east-1"
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

    const apiCertificate = new aws.acm.Certificate("AstroApiCertificate", {
      domainName: domain.api,
      validationMethod: "DNS"
    }, { provider: cloudFrontProvider });

    const apiCertificateRecords = apiCertificate.domainValidationOptions.apply((options) => options.map((option, index) =>
      new aws.route53.Record(`AstroApiCertificateValidation${index}`, {
        zoneId: hostedZoneId,
        name: option.resourceRecordName,
        type: option.resourceRecordType,
        ttl: 60,
        records: [option.resourceRecordValue]
      })
    ));

    const apiCertificateValidation = new aws.acm.CertificateValidation("AstroApiCertificateValidation", {
      certificateArn: apiCertificate.arn,
      validationRecordFqdns: apiCertificateRecords.apply((records) => records.map((record) => record.fqdn))
    }, { provider: cloudFrontProvider });

    const apiDistribution = new aws.cloudfront.Distribution("AstroApiDistribution", {
      aliases: [domain.api],
      origins: [{
        domainName: api.url.apply((url) => new URL(url).hostname),
        originId: "AstroApiGateway",
        customOriginConfig: {
          httpPort: 80,
          httpsPort: 443,
          originProtocolPolicy: "https-only",
          originSslProtocols: ["TLSv1.2"]
        }
      }],
      defaultCacheBehavior: {
        targetOriginId: "AstroApiGateway",
        viewerProtocolPolicy: "allow-all",
        allowedMethods: ["GET", "HEAD", "OPTIONS", "PUT", "PATCH", "POST", "DELETE"],
        cachedMethods: ["GET", "HEAD"],
        cachePolicyId: aws.cloudfront.getCachePolicyOutput({
          name: "Managed-CachingDisabled"
        }).id,
        originRequestPolicyId: aws.cloudfront.getOriginRequestPolicyOutput({
          name: "Managed-AllViewerExceptHostHeader"
        }).id
      },
      viewerCertificate: {
        acmCertificateArn: apiCertificate.arn,
        sslSupportMethod: "sni-only",
        minimumProtocolVersion: "TLSv1.2_2021"
      },
      restrictions: {
        geoRestriction: {
          restrictionType: "none"
        }
      },
      enabled: true,
      isIpv6Enabled: true,
      comment: `AstroWeather API ${$app.stage}`
    }, { dependsOn: [apiCertificateValidation] });

    for (const type of ["A", "AAAA"] as const) {
      new aws.route53.Record(`AstroApi${type}Alias`, {
        zoneId: hostedZoneId,
        type,
        name: domain.api,
        aliases: [{
          name: apiDistribution.domainName,
          zoneId: apiDistribution.hostedZoneId,
          evaluateTargetHealth: false
        }]
      });
    }

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
      apiUrl: `https://${domain.api}`,
      siteUrl: web.url,
      forecastDataTableName: forecastData.name
    };
  }
});
