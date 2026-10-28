// Who may call the signed-in routes. The Cognito pool is shared with other apps and anyone may register in it, so
// a valid token is not enough: the user must be in one of the groups this stage accepts (ACCESS_GROUPS, set in
// sst.config.ts from infra/stage.ts). prod accepts only astroweather; other stages also astroweather-test.
// The token's client is already checked by the API's JWT authorizer (audience: this stage's app client).
//
// The device route reuses the forecast handler behind the device-key Lambda authorizer; a request API Gateway
// let in through that authorizer passes. Anything else (no authorizer at all) is refused.

type Claims = Record<string, unknown> | undefined;

type AuthorizedEvent = {
  requestContext?: { authorizer?: { jwt?: { claims?: Claims }; lambda?: unknown } };
};

// HTTP API passes array claims flattened to a string, e.g. "[a b]".
export function groupsOf(claims: Claims): string[] {
  const raw = claims?.["cognito:groups"];
  if (Array.isArray(raw)) return raw.map(String);
  if (typeof raw !== "string" || !raw.trim()) return [];
  return raw.trim().replace(/^\[|\]$/g, "").split(/[\s,"]+/).filter(Boolean);
}

// No ACCESS_GROUPS means nobody gets in.
export function allowedGroups(env: string | undefined = process.env.ACCESS_GROUPS): string[] {
  return (env ?? "").split(",").map((g) => g.trim()).filter(Boolean);
}

export function hasAccess(event: AuthorizedEvent, allowed: string[] = allowedGroups()): boolean {
  const authorizer = event.requestContext?.authorizer;
  if (authorizer?.jwt) return groupsOf(authorizer.jwt.claims).some((g) => allowed.includes(g));
  return authorizer?.lambda !== undefined;
}

export function withUserAccess<E, R>(handler: (event: E) => Promise<R>) {
  return async (event: E) => {
    if (!hasAccess(event as AuthorizedEvent)) {
      const claims = (event as AuthorizedEvent).requestContext?.authorizer?.jwt?.claims;
      console.warn("Access denied: none of the accepted groups", { groups: groupsOf(claims), sub: claims?.sub });
      return {
        statusCode: 403,
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ message: "This account has no access to AstroWeather" })
      };
    }
    return handler(event);
  };
}
