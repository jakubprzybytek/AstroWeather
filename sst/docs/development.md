# Development and Deployment

All commands run from the `sst/` directory unless stated otherwise.

## Prerequisites

- Node.js and npm.
- Dependencies installed in both packages that SST builds:

  ```bash
  npm install
  npm --prefix packages/web install
  ```

- AWS credentials in the default AWS profile. They need the permissions in
  [deployment-role-policy.json](deployment-role-policy.json), which is the
  source of the `AstroWeather-Deployment` managed IAM policy. The region comes
  from the AWS profile configuration; `sst.config.ts` does not set one.

## Stages

Every SST stage is a separate copy of the whole app: API, CloudFront
distribution, DynamoDB table, ingestion schedule, and web site.

| Stage | Web | API | Purpose |
|---|---|---|---|
| `prod` | `https://astroweather.albedoonline.com` | `https://api.astroweather.albedoonline.com` | Live app; resources are retained on removal |
| `int` | `https://int.astroweather.albedoonline.com` | `https://api.int.astroweather.albedoonline.com` | Shared integration stage; deploy here before `prod` |
| personal (default) | `https://<stage>.astroweather.albedoonline.com` | `https://api.<stage>.astroweather.albedoonline.com` | Used by `sst dev` and commands run without `--stage` |

Stages other than `prod` are removed completely by `npm run remove`,
including the stored forecast data.

## Local Development

```bash
npm run dev
```

This starts `sst dev` on your personal stage. Lambda functions run locally
through SST's live mode, and the web UI dev server gets `VITE_API_URL` from SST.
See [packages/web/README.md](../packages/web/README.md) for running the web UI
against a separately started API.

## Deploying

Pass the stage to the npm script after `--`:

```bash
# Integration
npm run deploy -- --stage int

# Production
npm run deploy -- --stage prod
```

A deployment builds the Lambda functions and the web UI (`npm run build` in
`packages/web`) and updates only the changed resources. It prints the stage
outputs:

| Output | Meaning |
|---|---|
| `apiUrl` | Public API hostname, served by CloudFront over HTTP and HTTPS |
| `siteUrl` | Web UI |
| `forecastDataTableName` | DynamoDB table used by the ingestion job and forecast API |
| `AstroApi` | Generated API Gateway URL, used by the integration tests |

The Lambda handler and web UI are deployed together, so an API change and its
UI change are always released at the same time.

### Recommended release sequence

1. Run the local suites: `npm run test` and `npm run test:web`.
2. Deploy to `int`: `npm run deploy -- --stage int`.
3. Run the integration suite against `int` (see below) and spot-check the
   public hostname, for example
   `curl -si http://api.int.astroweather.albedoonline.com/astro/krakow`.
4. Deploy to `prod` and repeat the checks against its hostname.

### Testing a deployed stage

`npm run test:integration` runs Vitest inside `sst shell`, which selects the
stage from the `SST_STAGE` environment variable:

```bash
# bash
SST_STAGE=int npm run test:integration
```

```powershell
# PowerShell
$env:SST_STAGE = "int"; npm run test:integration; Remove-Item Env:SST_STAGE
```

See [testing.md](testing.md) for what the suites cover.

## Removing a Stage

```bash
npm run remove -- --stage <stage>
```

Use this for personal or temporary stages. `prod` is configured with
`removal: "retain"`, so its resources are kept even if the stage is removed.

## Troubleshooting

- **IAM authorization errors during deploy** (`AccessDenied`, `is not authorized
  to perform`): the deployment policy is missing an action. Follow the
  `update-aws-deployment-policy` skill in `.github/skills/`, which adds the
  smallest required permission to `deployment-role-policy.json` and publishes
  it.
- **Stale lock after an interrupted deploy**: confirm no other deployment is
  running, then run `npx sst unlock --stage <stage>`.
