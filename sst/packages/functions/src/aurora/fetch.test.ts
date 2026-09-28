import { afterEach, describe, expect, test, vi } from "vitest";
import { fetchSourceDocument } from "./fetch";

const originalFetch = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = originalFetch;
});

describe("fetchSourceDocument", () => {
  test("returns the body and last-modified header", async () => {
    globalThis.fetch = vi.fn().mockResolvedValue(new Response("data", {
      status: 200,
      headers: { "last-modified": "Mon, 28 Sep 2026 19:05:18 GMT" }
    }));

    await expect(fetchSourceDocument("https://example.test/file", "Example")).resolves.toEqual({
      body: "data",
      lastModified: "Mon, 28 Sep 2026 19:05:18 GMT"
    });
  });

  test("retries one transient server failure", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response("temporary failure", { status: 503 }))
      .mockResolvedValueOnce(new Response("data", { status: 200 }));
    globalThis.fetch = fetchMock;

    await expect(fetchSourceDocument("https://example.test/file", "Example")).resolves.toMatchObject({ body: "data" });
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  test("does not retry a client failure", async () => {
    const fetchMock = vi.fn().mockResolvedValue(new Response("missing", { status: 404 }));
    globalThis.fetch = fetchMock;

    await expect(fetchSourceDocument("https://example.test/file", "Example")).rejects.toThrow("Example request failed with HTTP 404");
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });
});
