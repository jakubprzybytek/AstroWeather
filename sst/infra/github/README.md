# GitHub Actions deploy role

This repo deploys to AWS following the [aws-github-deploy standard](https://github.com/jakubprzybytek/agent-playbook/blob/main/skills/aws-github-deploy/STANDARD.md).

| | |
|---|---|
| Role | `GitHub-AstroWeather` |
| Policies | `SST-Deployment` (shared), `AstroWeather-Deployment` (this repo, [deploy-policy.json](deploy-policy.json)) |
| SST app name | `astroweather-api` |
| Resource prefix in the policy | `astro*` (see deviations) |
| Region | `eu-west-1` (CloudFront certificate in `us-east-1`) |
| Workflows that deploy | `.github/workflows/deploy-sst.yml` (stage `int`; `prod` is deployed by hand) |
| GitHub | secret `AWS_ACCOUNT_ID`, variable `AWS_REGION` |

`deploy-policy.json` has `ACCOUNT_ID` in place of the account number. The rendered policy and the trust policy are in `private/` (gitignored).

## What the project policy covers

The app is an API Gateway (HTTP API) with Lambda handlers and two authorizers, EventBridge Scheduler ingestion jobs, a DynamoDB table, and a static web site on CloudFront.

- `Identity`: `sts:GetCallerIdentity`.
- `SstState`: SST parameters under `/sst/*`.
- `Buckets`: S3 on the site bucket (`astro*`) and the SST asset bucket (`sst-asset-*`, Lambda code uploads).
- `Functions`: Lambda functions `astro*`.
- `Logs`: CloudWatch log groups in eu-west-1.
- `ApiAccessLogDelivery`: log deliveries for the API's access logs; these actions only accept `Resource: "*"`.
- `ApiGateway`: API Gateway in eu-west-1 (HTTP API, routes, authorizers, custom domain).
- `Table`: DynamoDB tables `astro*`.
- `Schedules`: EventBridge Scheduler schedules `astro*` (any group).
- `Edge`: CloudFront (distribution, function, KeyValueStore), ACM certificates and Route 53 records. Account-wide; these services can't be scoped usefully.
- `Roles`: IAM roles `astro*` for the functions and schedules.
- `PassRoles`: pass `astro*` roles to Lambda and EventBridge Scheduler only.

## Deviations from the standard

- **Prefix `astro*` instead of `astroweather-api-*`.** SST shortens names that exceed the 64-character limit of Lambda functions and IAM roles by cutting the app name, so the same app appears as `astroweather-api-int-…`, `astroweather-a-int-…`, `astroweather-int-…` and `astrowea-int-…`. No other app in the account starts with `astro`.
- The SST app lives in `sst/`, so `infra/github/` is under `sst/` and `setup-role.sh` runs from there.

## Changing the role or policy

From `sst/`, with admin credentials (`aws login`):

```sh
~/.claude/skills/aws-github-deploy/scripts/setup-role.sh AstroWeather
```

The script is safe to re-run: it renders the documents, publishes a new policy version if the file changed, updates the trust policy and tags, and sets the GitHub secret and variable.

## History

- Up to v67 the policy was built action by action with `Resource: "*"` (one version per missing permission). v68 (2026-10-25) moved it to the standard: the same services, whole-service actions scoped to `astro*`.
