import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, test, vi } from "vitest";
import { fetchClearOutsideHtml, parseClearOutside } from "./clearoutside.js";

const fixture = readFileSync(
  fileURLToPath(new URL("./__fixtures__/clearoutside.html", import.meta.url)),
  "utf8"
);

const originalFetch = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = originalFetch;
  vi.useRealTimers();
});

describe("parseClearOutside", () => {
  test("parses a noon-to-noon night into hourly weather samples", () => {
    const [night] = parseClearOutside(fixture);

    expect(night.nightId).toBe("2026-09-11");
    expect(night.hours).toHaveLength(24);
    expect(night.hours[0]).toEqual({
      hour: 12,
      timestampUtc: "2026-09-11T09:00:00.000Z",
      temperatureC: 36,
      cloudCoverTotalPct: 0,
      precipitationProbabilityPct: 0,
      thunderstormRisk: false
    });
    expect(night.hours[12].timestampUtc).toBe("2026-09-11T21:00:00.000Z");
    expect(night.hours[6].thunderstormRisk).toBe(true);
    expect(night.hours[6].precipitationProbabilityPct).toBe(40);
  });

  test("fails closed when a required weather row is missing", () => {
    const malformed = fixture.replace("Total Clouds (% Sky Obscured)", "Clouds");

    expect(() => parseClearOutside(malformed)).toThrow(
      "Clear Outside page is missing the Total Clouds row"
    );
  });

  test("retries one transient server failure", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response("temporary failure", { status: 503 }))
      .mockResolvedValueOnce(new Response("forecast", { status: 200 }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(50, 20)).resolves.toBe("forecast");
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  test("does not retry a non-retryable client failure", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("bad request", { status: 400 }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(50, 20)).rejects.toThrow("HTTP 400");
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  test("does not immediately retry a rate-limited response", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("too many requests", {
      status: 429,
      headers: { "retry-after": "3600" }
    }));
    globalThis.fetch = fetchMock;

    await expect(fetchClearOutsideHtml(27.9, 34.3)).rejects.toThrow(
      "HTTP 429; retry after 3600"
    );
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  test("retries after a request timeout", async () => {
    vi.useFakeTimers();
    const fetchMock = vi.fn((_: string, init?: RequestInit) => {
      if (fetchMock.mock.calls.length === 1) {
        return new Promise<Response>((_, reject) => {
          init?.signal?.addEventListener("abort", () => reject(new DOMException("aborted", "AbortError")));
        });
      }

      return Promise.resolve(new Response("forecast", { status: 200 }));
    });
    globalThis.fetch = fetchMock;

    const result = fetchClearOutsideHtml(50, 20);
    await vi.advanceTimersByTimeAsync(15_000);
    await vi.advanceTimersByTimeAsync(400);

    await expect(result).resolves.toBe("forecast");
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });
});
