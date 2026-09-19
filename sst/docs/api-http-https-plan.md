# API HTTP and HTTPS Availability Plan

## Goal

Make every AstroWeather API route reachable from both URL schemes at the same
stage-specific hostname:

- `https://api.astroweather.albedoonline.com` in production;
- `https://api.<stage>.astroweather.albedoonline.com` in other stages;
- the equivalent `http://` URLs, returning API data without a redirect.

HTTP and HTTPS must expose the same routes, status codes, headers, and response
bodies. This includes `GET /configurations`, `GET /astro/{configurationId}`,
and `POST /tools/clearoutside`. The public client-to-CloudFront connection may
be plaintext for HTTP; the CloudFront-to-API Gateway origin connection remains
encrypted.

## Current State

- `sst.config.ts` gives `AstroApi` a stage-specific custom domain. SST provisions
  the API Gateway domain, ACM certificate, and Route 53 alias, so the intended
  custom endpoint already supports HTTPS.
- API Gateway custom domains do not provide a listener on port 80. An HTTP
  request therefore needs an edge layer in front of API Gateway.
- The web application already receives `VITE_API_URL` with an explicit
  `https://` scheme and should continue to call HTTPS directly.
- The checked-in deployment role already includes broad CloudFront, ACM, and
  Route 53 management permissions, but deployment must prove whether any
  additional actions are needed.

## Decision

Put a CloudFront distribution in front of the API custom hostname and configure
its viewer protocol policy as `allow-all`.

CloudFront should use the API Gateway generated `execute-api` hostname as an
HTTPS-only origin. Route 53 should point the public API hostname at CloudFront,
not directly at the API Gateway regional custom domain. This avoids a DNS name
collision and keeps the origin connection encrypted.

Do not implement scheme redirects in Lambda or at the edge. CloudFront must
forward HTTP viewer requests to the API Gateway origin and return the origin's
status, headers, and body unchanged. API Gateway itself remains reachable from
CloudFront through HTTPS only.

CloudFront serves HTTP requests directly: clients receive the final API
response, not a `301`, `302`, `307`, or `308` redirect.

Retain API Gateway as the application origin. It continues to own routing,
CORS, throttling, and Lambda invocation. Do not introduce Lambda Function URLs,
combine the handlers into a router Lambda, or change API Gateway event shapes as
part of this work.

```text
HTTP or HTTPS client
  |
  v
CloudFront (public API hostname, allow-all)
  |
  | HTTPS
  v
API Gateway generated execute-api endpoint
  |
  v
Existing route Lambdas
```

## Implementation Steps

### 1. Confirm the deployed baseline

- Deploy or inspect a non-production stage and record the effective API Gateway
  URL, custom API hostname, certificate status, and Route 53 records.
- Verify that each HTTPS route works on the custom hostname before changing DNS.
- Run `curl` against the HTTP custom hostname and record the current failure.
  The changed behavior must be a final API response rather than a redirect.
- Confirm that the API Gateway URL has no stage path that CloudFront must add as
  an origin path.

### 2. Add the API edge distribution

- Keep `AstroApi` and all route definitions unchanged, but remove ownership of
  the public API hostname from its `domain` property.
- Add one CloudFront distribution for the API in `sst.config.ts`, using the
  generated API Gateway hostname as a custom HTTPS origin.
- Configure the default behavior with:
  - viewer protocol policy `allow-all`;
  - allowed methods `GET`, `HEAD`, `OPTIONS`, `PUT`, `PATCH`, `POST`, and
    `DELETE` so current and future API methods are not blocked at the edge;
  - HTTPS-only origin protocol;
  - caching disabled unless a route-specific cache policy is designed later;
  - all query strings required by API Gateway forwarded;
  - request headers needed for CORS and content negotiation forwarded;
  - request bodies forwarded for `POST` and other write methods;
  - the viewer `Host` header excluded when the origin is the generated
    `execute-api` hostname, allowing CloudFront to send the origin hostname.
- Use the AWS managed `CachingDisabled` cache policy and
  `AllViewerExceptHostHeader` origin request policy where the SST abstraction
  exposes them. Otherwise create equivalent policies explicitly.
- Preserve API Gateway's existing one-request burst and one-request-per-second
  throttling settings. CloudFront must not become a separate cache-based bypass
  of route behavior.

Implement the distribution with the narrowest SST component that exposes the
required origin and `allow-all` viewer behavior. If no SST component exposes
those settings, define the CloudFront distribution, certificate, and aliases
with Pulumi AWS resources in `sst.config.ts`. Inspect the synthesized diff
before deployment; do not replace `AstroApi`.

### 3. Move the custom domain to CloudFront

- Provision or reuse an ACM certificate for the API hostname in `us-east-1`, as
  CloudFront requires. Validate it through the existing Route 53 hosted zone.
- Attach the stage-specific API hostname as a CloudFront alternate domain name.
- Change the existing Route 53 `A` and `AAAA` aliases to target CloudFront.
- Ensure only one resource owns each API DNS record during deployment. Sequence
  the resource dependencies so the certificate and distribution are ready
  before DNS switches.
- Keep the API Gateway generated hostname as the distribution origin. Decide
  separately whether direct access to that generated URL is acceptable; it is
  not necessary to solve HTTP entry-point support.

### 4. Keep application and CORS behavior secure

- Continue injecting `VITE_API_URL=https://<api-hostname>` into the web build.
  The browser should always use the encrypted endpoint.
- Keep deployed web origins HTTPS-only in API Gateway CORS settings. Do not add
  `http://astroweather...` unless a browser application is intentionally hosted
  at that origin.
- Retain local HTTP origins for Vite development.
- Confirm that CloudFront forwards `Origin`, preflight headers, and API Gateway
  CORS response headers without replacing them.
- Do not add HSTS until every relevant subdomain is confirmed HTTPS-ready. If it
  is later added, start with a short `max-age` and do not use `includeSubDomains`
  or preload as part of this change.

### 5. Add transport-level integration checks

Add a deployment integration test or script that checks the public custom
hostname rather than only the generated API URL:

- HTTPS `GET /configurations` returns `200` and the existing JSON content type.
- HTTPS `GET /astro/<known-id>` returns `200` and
  `text/plain; charset=utf-8`.
- HTTP `GET /configurations` returns the same final status, content type, and
  body as the HTTPS request, with no `Location` header.
- HTTP `GET /astro/<known-id>` returns the same final status, content type, and
  body shape as the HTTPS request, with no redirect.
- HTTP `POST /tools/clearoutside` reaches the Lambda with the original method
  and body and returns the API response directly.
- HTTPS `OPTIONS` reaches API Gateway and returns the expected CORS headers.
- The certificate hostname matches and the TLS chain is valid.
- Neither scheme redirects to the other.

Keep Lambda unit tests unchanged because scheme handling is infrastructure behavior.
Disable automatic redirect following in HTTP assertions so an accidental edge
redirect fails the tests.

### 6. Deploy safely

- Deploy to a non-production stage first.
- If deployment fails with an explicit authorization error, use the repository's
  `update-aws-deployment-policy` skill to add only the denied action to
  `docs/deployment-role-policy.json`, publish the policy, and retry.
- Wait for the CloudFront distribution and ACM validation to complete before
  transport tests. DNS should have a low enough TTL for the planned cutover.
- Validate the web application against the HTTPS API after the DNS switch.
- Deploy production, repeat all transport checks, and inspect CloudFront and API
  Gateway metrics for elevated 4xx/5xx responses.

## Security Constraints

HTTP exposes request paths, query strings, request bodies, response
bodies, and identifiers to interception and modification. It must be approved as
an explicit security exception and must not be used for authenticated,
administrative, personal, or otherwise sensitive data. Do not add credentials,
cookies, bearer tokens, API keys, or sensitive parameters to HTTP requests. The
web application and all capable clients should continue to prefer HTTPS.

Document HTTP support as compatibility behavior for constrained clients. Any
future authentication or sensitive API route must be HTTPS-only or must move to
a separate hostname whose CloudFront behavior uses `redirect-to-https`.

## Architecture Exclusions

- Do not replace API Gateway with Lambda Function URLs.
- Rewriting the three route handlers or their event contracts.
- Combining routes into one router Lambda.
- Adding response caching, authentication, or HSTS.
- Making the generated API Gateway hostname accept HTTP.

## Validation Sequence

1. `npm run test:unit`
2. `npm run test:web`
3. Preview the SST infrastructure diff for a non-production stage.
4. Deploy the non-production stage.
5. Run existing integration tests against the HTTPS custom hostname.
6. Run the direct HTTP response, HTTPS transport, POST preservation, and CORS
  checks.
7. Open the deployed web application and verify its requests use HTTPS directly.
8. Repeat steps 4 through 7 for production.

## Rollback

Restore the API Gateway custom-domain association and Route 53 aliases from the
last known-good deployment, then remove the API CloudFront distribution only
after DNS points back to API Gateway. The generated API Gateway URL remains the
emergency HTTPS validation endpoint during rollback.

Do not change the web application to use HTTP during rollback.

## Completion Criteria

- The stage-specific public API hostname serves every route over valid HTTPS.
- The same hostname serves every route directly over HTTP without redirecting.
- Equivalent HTTP and HTTPS requests produce the same API status, headers, and
  body, apart from transport-specific headers added by CloudFront.
- HTTP `POST` requests retain their method and body.
- API Gateway throttling and CORS behavior remain unchanged.
- The deployed web application calls HTTPS directly.
- The plaintext security exception and HTTPS-only requirement for future
  sensitive routes are documented.
- Automated transport checks pass against a non-production and production
  custom hostname.
