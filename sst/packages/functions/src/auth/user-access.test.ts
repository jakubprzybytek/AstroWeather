import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { allowedGroups, groupsOf, withUserAccess } from "./user-access";

const jwt = (groups: unknown) => ({ requestContext: { authorizer: { jwt: { claims: { sub: "u1", "cognito:groups": groups } } } } });
const ok = withUserAccess(async () => ({ statusCode: 200 }));

beforeEach(() => {
  vi.stubEnv("ACCESS_GROUPS", "astroweather,astroweather-test");
  vi.spyOn(console, "warn").mockImplementation(() => undefined);
});
afterEach(() => {
  vi.unstubAllEnvs();
  vi.restoreAllMocks();
});

describe("withUserAccess", () => {
  test("lets in users of an accepted group", async () => {
    expect((await ok(jwt("[astroweather-test]"))).statusCode).toBe(200);
  });

  test("rejects users of other groups, without groups, and requests with no authorizer", async () => {
    expect((await ok(jwt("[catalyst-viewer]"))).statusCode).toBe(403);
    expect((await ok(jwt(undefined))).statusCode).toBe(403);
    expect((await ok({})).statusCode).toBe(403);
  });

  test("lets in the device route, authorized by its key", async () => {
    expect((await ok({ requestContext: { authorizer: { lambda: {} } } })).statusCode).toBe(200);
  });

  test("applies the stage's groups: prod rejects the test group, none rejects everyone", async () => {
    vi.stubEnv("ACCESS_GROUPS", "astroweather");
    expect((await ok(jwt("[astroweather-test]"))).statusCode).toBe(403);
    vi.stubEnv("ACCESS_GROUPS", "");
    expect((await ok(jwt("[astroweather]"))).statusCode).toBe(403);
  });
});

describe("groupsOf", () => {
  test("reads the groups claim in each shape it comes in", () => {
    expect(groupsOf({ "cognito:groups": "[astroweather astroweather-test]" })).toEqual(["astroweather", "astroweather-test"]);
    expect(groupsOf({ "cognito:groups": ["astroweather"] })).toEqual(["astroweather"]);
    expect(groupsOf({ "cognito:groups": "" })).toEqual([]);
    expect(allowedGroups(" a, b ")).toEqual(["a", "b"]);
  });
});
