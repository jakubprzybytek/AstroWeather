---
name: update-aws-deployment-policy
description: 'Iteratively update the AstroWeather AWS deployment IAM policy by deploying SST, extracting AccessDenied or not-authorized actions, minimally editing the policy JSON, publishing it with the rob AWS profile, and retrying with the default profile. Use when deployment permissions are missing, SST deploy fails with IAM errors, or docs/deployment-role-policy.json needs synchronization with arn:aws:iam::198805281865:policy/AstroWeather-Deployment.'
argument-hint: 'Optional SST stage, defaults to int'
---

# Update AWS Deployment Policy

Use this workflow to make the AstroWeather deployment policy cover the permissions actually exercised by SST.

## Repository Configuration

- Deployment command: `npm run deploy -- --stage <stage>`
- Default stage: `int`
- Policy source: `docs/deployment-role-policy.json`
- Managed policy ARN: `arn:aws:iam::198805281865:policy/AstroWeather-Deployment`
- Deployment credentials: default AWS profile
- Policy-administration credentials: `rob` AWS profile

Treat the JSON file as the source of truth. Do not update AWS without making the equivalent repository edit.

## Safety Rules

- Run deployments with `AWS_PROFILE` explicitly unset. Never deploy with `rob` when testing deployment permissions.
- Use `AWS_PROFILE=rob` only for read-only IAM inspection and publishing the managed policy.
- Add permissions only when an actual deployment error identifies the denied AWS action.
- Do not interpret configuration errors, missing resources, provider bugs, concurrent updates, or timeouts as missing IAM permissions.
- Prefer adding an action to an existing service statement. Add a new statement only when no suitable statement exists.
- Preserve existing user changes and avoid unrelated policy cleanup.
- Keep `iam:PassRole` constrained with `iam:PassedToService`. Do not replace it with unconditional pass-role access.
- Never attach `AdministratorAccess`, `PowerUserAccess`, or service `*FullAccess` policies as part of this workflow unless the user explicitly requests that migration.
- Never delete the default managed-policy version. When the five-version limit is reached, delete only the oldest non-default version.
- Do not remove, recreate, or otherwise mutate application resources outside the requested SST deployment.
- Continue until deployment succeeds, the failure is not permission-related, or user input is genuinely required.

## Procedure

### 1. Establish Current State

1. Read `docs/deployment-role-policy.json` before editing it.
2. Validate it with `jq empty docs/deployment-role-policy.json`.
3. Using `AWS_PROFILE=rob`, inspect the managed policy's current default version and document.
4. Note whether the local JSON has unpublished changes. Do not overwrite newer user edits.

### 2. Deploy With Restricted Credentials

Run:

```bash
env -u AWS_PROFILE npm run deploy -- --stage <stage>
```

Capture the exit code and the complete first actionable error. If output is large, redirect it to a temporary file outside the repository and extract `AccessDenied`, `not authorized`, `UnauthorizedOperation`, and surrounding resource details.

If the deployment succeeds, report the SST outputs and stop.

If deployment reports multiple independent IAM denials in one completed run, they may be handled together. Otherwise address the first denial and retry.

### 3. Classify the Failure

Proceed with a policy edit only for an explicit authorization failure that identifies an AWS action, such as:

- `AccessDeniedException`
- `AccessDenied`
- `UnauthorizedOperation`
- `is not authorized to perform: service:Action`
- `no identity-based policy allows the service:Action action`

Do not edit IAM for errors such as:

- resource not found
- invalid or empty configuration values
- DNS or certificate validation failures without an authorization error
- SST concurrent update or stale lock
- build, TypeScript, test, or provider errors
- a timeout with no authorization evidence

Diagnose those separately and tell the user why the IAM loop paused.

### 4. Make the Smallest Policy Edit

1. Confirm the denied action is absent from the local policy.
2. Add the exact action to the matching service statement.
3. Use the narrowest practical resource scope consistent with SST's generated names. Preserve existing statement scope unless changing it is necessary.
4. For `iam:PassRole`, add or extend a service-specific conditioned statement instead of granting unconditional access.
5. Validate immediately:

```bash
jq empty docs/deployment-role-policy.json
git diff --check
```

Also check that the new action is not duplicated.

### 5. Publish With `rob`

Use noninteractive AWS CLI settings:

```bash
export AWS_PROFILE=rob
export AWS_PAGER=''
export AWS_CLI_PAGER=''
```

1. List policy versions.
2. If there are five versions, select the oldest version where `IsDefaultVersion` is `false` and delete only that version.
3. Create a new policy version from the repository file and set it as default:

```bash
aws iam create-policy-version \
  --policy-arn arn:aws:iam::198805281865:policy/AstroWeather-Deployment \
  --policy-document file://docs/deployment-role-policy.json \
  --set-as-default
```

4. Retrieve the newly created version document and verify the exact added action is present.
5. Do not trust an ambiguous or timed-out publication result. Query IAM directly to determine whether a new default version was created before retrying or deleting another version.

### 6. Retry

Return to step 2 using the default profile. Repeat the deploy, classify, edit, validate, publish, and verify loop until deployment succeeds or encounters a non-IAM blocker.

If a killed deployment leaves a stale SST lock, first confirm no deployment process is active. Then clear only the requested stage lock with:

```bash
npx sst unlock --stage <stage>
```

Do not unlock while another deployment is still active.

## Completion Report

Report:

- whether deployment succeeded
- stage deployed
- policy actions added
- final managed-policy version ID
- deployment outputs or remaining non-IAM blocker
- files changed

Do not claim success based only on publishing the policy; the final check is a successful deployment using the default AWS profile.
