import { describe, expect, test, vi } from "vitest";
import { utcSpan } from "../aurora/spans";
import { createSourceToolHandler, type ToolLocation } from "./handler";

const DAY = 24 * 60 * 60 * 1000;

function tool(load = vi.fn().mockResolvedValue({ spans: [], lastModified: null })) {
  return {
    load,
    handler: createSourceToolHandler({
      url: "https://example.test/source",
      failureMessage: "Unable to load example data",
      load,
      now: () => new Date("2026-09-28T20:00:00Z")
    })
  };
}

describe("createSourceToolHandler", () => {
  test("resolves a configuration's coordinates and timezone", async () => {
    const { handler, load } = tool();

    const response = await handler({ body: JSON.stringify({ configurationId: "krakow" }) });

    expect(response.statusCode).toBe(200);
    expect(load).toHaveBeenCalledWith<[ToolLocation]>({
      configurationId: "krakow",
      latitude: 50.0647,
      longitude: 19.945,
      timezone: "Europe/Warsaw"
    });
    expect(JSON.parse(response.body)).toMatchObject({
      configurationId: "krakow",
      coordinates: { latitude: 50.0647, longitude: 19.945 },
      timezone: "Europe/Warsaw",
      source: { url: "https://example.test/source", fetchedAt: "2026-09-28T20:00:00.000Z", lastModified: null }
    });
  });

  test("accepts coordinates with a timezone", async () => {
    const { handler, load } = tool();

    const response = await handler({ body: JSON.stringify({ latitude: 27.9, longitude: 34.3, timezone: "Africa/Cairo" }) });

    expect(response.statusCode).toBe(200);
    expect(load).toHaveBeenCalledWith({ latitude: 27.9, longitude: 34.3, timezone: "Africa/Cairo" });
  });

  test("groups the loaded spans into the location's local nights", async () => {
    const { handler } = tool(vi.fn().mockResolvedValue({
      spans: [{ ...utcSpan(new Date(Date.UTC(2026, 9, 4)), DAY), largestKp: 4 }],
      lastModified: "Mon, 28 Sep 2026 20:12:28 GMT",
      issuedAt: "2026-09-28T02:21:00.000Z"
    }));

    const body = JSON.parse((await handler({ body: JSON.stringify({ configurationId: "wroclaw" }) })).body);

    expect(body.source.issuedAt).toBe("2026-09-28T02:21:00.000Z");
    expect(body.nights.map((night: { nightId: string }) => night.nightId)).toEqual(["2026-10-03", "2026-10-04"]);
    expect(body.nights[0].spans[0]).toMatchObject({ start: "2026-10-04T00:00:00.000Z", largestKp: 4 });
  });

  test("rejects ambiguous or invalid input", async () => {
    const { handler } = tool();

    expect((await handler({ body: "{" })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({}) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ configurationId: "missing" }) })).statusCode).toBe(404);
    expect((await handler({ body: JSON.stringify({ configurationId: "krakow", latitude: 1, longitude: 2 }) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ latitude: 100, longitude: 2, timezone: "Europe/Warsaw" }) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ latitude: 1, longitude: 2 }) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ latitude: 1, longitude: 2, timezone: "Mars/Olympus" }) })).statusCode).toBe(400);
  });

  test("reports a source failure as a bad gateway", async () => {
    const { handler } = tool(vi.fn().mockRejectedValue(new Error("GFZ Hp60 request failed with HTTP 503")));

    const response = await handler({ body: JSON.stringify({ configurationId: "wroclaw" }) });

    expect(response.statusCode).toBe(502);
    expect(JSON.parse(response.body)).toEqual({ message: "GFZ Hp60 request failed with HTTP 503" });
  });
});
