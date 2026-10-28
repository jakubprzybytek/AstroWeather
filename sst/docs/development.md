# Development

The server (API, ingestion and web UI) is developed and delivered per the [continuous-delivery standard](https://github.com/jakubprzybytek/agent-playbook/blob/main/skills/continuous-delivery/STANDARD.md): develop on `dev`, every push to `main` that changes `sst/` goes through `int` to `prod`. This file holds what is specific to this app. All commands run from the `sst/` directory.

## Stages

| Stage | Web | API | Notes |
|---|---|---|---|
| `dev` | https://dev.astroweather.albedoonline.com (or http://localhost:5173 with `npm run dev`) | https://api.dev.astroweather.albedoonline.com | no ingestion |
| `int` | https://int.astroweather.albedoonline.com | https://api.int.astroweather.albedoonline.com | the pipeline's tests; no ingestion |
| `prod` | https://astroweather.albedoonline.com | https://api.astroweather.albedoonline.com | live: the device reads it; ingestion every 6 h and the nowcast every 10 min |

What differs per stage is decided in `infra/stage.ts`: the domains, whether the schedules run (`scheduleEnabled`, prod only) and the Cognito groups the API accepts. Without ingestion the forecast still answers, with `?` for the weather and aurora values; tests check the format, not the values.

`prod` keeps its resources (`retain` + `protect`), including the `ForecastData` table. The table only holds fetched forecasts with a TTL, so the next ingestion run refills it.

Prerequisites: `npm install` and `npm --prefix packages/web install` (the web UI has its own lockfile); AWS credentials from `aws login`.

## Run locally

```sh
npm run dev -- --stage dev        # sst dev: Lambdas on your machine, web UI on http://localhost:5173
npm run deploy -- --stage dev     # full deployment (agent sessions, CloudFront)
```

See [packages/web/README.md](../packages/web/README.md) for running the web UI against a separately started API.

## Sign-in and access

- **Pool and app client:** users sign in with the shared Cognito pool `AlbedoUserPool` (`eu-west-1_IVai0KEAA`), which is managed by hand. Each stage has its own app client (`astroweather-<stage>-web`), which the API's JWT authorizer checks as the audience.
- **Groups:** anyone may register in the shared pool, so the signed-in routes also check the group (`packages/functions/src/auth/user-access.ts`):
  - `prod`: `astroweather` only
  - `dev` and `int`: `astroweather` and `astroweather-test`
- **No sign-up in the web UI:** a self-registered account would get 403 anyway.
- **Giving someone access:** `aws cognito-idp admin-add-user-to-group --user-pool-id eu-west-1_IVai0KEAA --username <user> --group-name astroweather`.

## Device key

The device route `/device/astro/<configurationId>` uses its own Lambda authorizer and the stage's SST secret `DeviceApiKey` (comma-separated, so a new key can be added before the device switches). Each stage needs it before its first deploy. Set it through stdin, never as an argument:

```sh
openssl rand -hex 16 | tr -d '\n' | ./node_modules/.bin/sst secret set DeviceApiKey --stage <stage>
```

The device keeps the host and key in its EEPROM, set on its USB console with `api host <host>` and `api key <key>` (see the firmware's [Console.md](../../firmware/HostControllerA/Docs/Console.md#api)), and sends the key as `?key=<key>`. To rotate: set `old,new`, deploy, run `api key <new>` on the device, then set `new` alone and deploy again.

## Tests

| Kind | Command |
|---|---|
| Unit, web | `npm test`, `npm run test:web` (the web build, `tsc -b`, is the type check) |
| Integration (API) | `npx sst shell --stage dev -- npx vitest run --project integration` |
| E2E | `npx sst shell --stage dev -- npm run test:e2e` |

- **Test user:** `astroweather-test@albedoonline.com`, group `astroweather-test`. Its password is in the SST secret `TestUserPassword` of `dev` and `int`.
- **How the tests use it:** the integration tests sign in with a plain password, which is allowed on the non-prod app clients only, and call the signed-in routes. The E2E tests sign in through the web UI once and reuse the session.
- **Never on prod:** both refuse to run on `prod`.
- **More:** what the suites cover is in [testing.md](testing.md).

## Delivery

`.github/workflows/continuous-delivery.yaml`, per the standard.

- **Releases:** tags `vM.N`, one per release, only going forward; shown in the web UI's footer. The old tag `v1` (another scheme) is ignored; releases continue from `v1.0`, tagged on the commit before the first pipeline run.
- **After the prod deploy:** the pipeline only checks that the site answers and that the API refuses a request without a token.
- **Rollback:** run Continuous Delivery manually from the release tag ("Use workflow from"; `gh workflow run continuous-delivery.yaml --ref vM.N`).

Ask before pushing changes that touch data, resources holding data, access, or cost (the standard, section 5). Here also: anything that changes the device route, its key or its response format (the device is in the field), the schedules, or the accepted groups.

Dependencies: Dependabot per the [dependabot standard](https://github.com/jakubprzybytek/agent-playbook/blob/main/skills/dependabot/STANDARD.md), for both lockfiles; pull requests run `.github/workflows/pull-request-checks.yaml`.

## Troubleshooting

- **IAM authorization errors during deploy** (`AccessDenied`, `is not authorized to perform`): the deployment policy is missing an action. Follow the "fix missing permissions" procedure of the `aws-github-deploy` skill ([agent-playbook](https://github.com/jakubprzybytek/agent-playbook/tree/main/skills/aws-github-deploy)): add the action to [infra/github/deploy-policy.json](../infra/github/deploy-policy.json) and publish it with `setup-role.sh`.
- **Stale lock after an interrupted deploy:** confirm no other deployment is running, then run `npx sst unlock --stage <stage>`.

## Deviations

- **The app lives in `sst/`, not the repo root:** the repo also holds the firmware and the PCB. The docs are in `sst/docs/`, and only changes under `sst/` (or to the workflow) start the pipeline.
- **No ingestion on `dev` and `int`:** the forecast there has no weather or aurora data. The live fetches are covered by unit tests against saved fixtures and by `npm run test:clearoutside`, a manual live scrape.
