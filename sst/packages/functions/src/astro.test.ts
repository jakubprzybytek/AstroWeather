import { describe, expect, test, vi } from "vitest";
import { createHandler } from "./astro";

function handlerAt(instant: string) {
  return createHandler({
    now: () => new Date(instant),
    readWeather: async () => new Map(),
    log: vi.fn()
  });
}

describe("astro handler", () => {
  test("renders the response time in the configuration's local timezone", async () => {
    const response = await handlerAt("2026-09-22T21:22:45.678Z")({ pathParameters: { configurationId: "krakow" } });

    expect(response.statusCode).toBe(200);
    expect(response.headers["content-type"]).toBe("text/plain; charset=utf-8");
    expect(response.body.split("\n").slice(0, 4)).toEqual([
      "protocol=1", "configurationId=krakow", "time=2026-09-22T23:22:45", ""
    ]);
  });

  test("uses the winter offset after the DST change", async () => {
    const response = await handlerAt("2026-12-01T23:30:00Z")({ pathParameters: { configurationId: "wroclaw" } });

    expect(response.body).toContain("\ntime=2026-12-02T00:30:00\n");
  });

  test("returns the versioned error without a time for an unknown configuration", async () => {
    const response = await handlerAt("2026-09-22T21:22:45Z")({ pathParameters: { configurationId: "unknown" } });

    expect(response.statusCode).toBe(404);
    expect(response.body).toBe("protocol=1\nerror=configuration_not_found\n");
  });
});
