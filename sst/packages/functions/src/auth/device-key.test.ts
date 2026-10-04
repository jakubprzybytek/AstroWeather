import { describe, expect, test, vi } from "vitest";

vi.mock("sst", () => ({ Resource: { DeviceApiKey: { value: "old-key, new-key" } } }));

const { handler, isValidKey } = await import("./device-key");

describe("isValidKey", () => {
  test("accepts any of the comma-separated keys", () => {
    expect(isValidKey("old-key", "old-key, new-key")).toBe(true);
    expect(isValidKey("new-key", "old-key, new-key")).toBe(true);
  });

  test("rejects a wrong, partial, missing or empty key", () => {
    expect(isValidKey("other-k", "old-key, new-key")).toBe(false);
    expect(isValidKey("old", "old-key")).toBe(false);
    expect(isValidKey(undefined, "old-key")).toBe(false);
    expect(isValidKey("", "old-key")).toBe(false);
  });

  test("does not treat an empty entry as a key", () => {
    expect(isValidKey("", ",old-key")).toBe(false);
  });
});

describe("handler", () => {
  test("returns the simple authorizer response", async () => {
    await expect(handler({ queryStringParameters: { key: "new-key" } })).resolves.toEqual({ isAuthorized: true });
    await expect(handler({ queryStringParameters: { key: "bad-key" } })).resolves.toEqual({ isAuthorized: false });
    await expect(handler({})).resolves.toEqual({ isAuthorized: false });
  });
});
