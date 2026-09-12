import { describe, expect, test, vi } from "vitest";
import { handler } from "./clearoutside";
import { fetchClearOutsideHtml, parseClearOutside } from "./weather/clearoutside";

vi.mock("./weather/clearoutside", () => ({
  fetchClearOutsideHtml: vi.fn(),
  parseClearOutside: vi.fn()
}));

const fetchHtmlMock = vi.mocked(fetchClearOutsideHtml);
const parseMock = vi.mocked(parseClearOutside);

describe("clearoutside handler", () => {
  test("resolves a configuration before fetching", async () => {
    fetchHtmlMock.mockResolvedValue("html");
    parseMock.mockReturnValue([{ nightId: "2026-09-11", hours: [] }]);

    const response = await handler({ body: JSON.stringify({ configurationId: "krakow-home" }) });

    expect(response.statusCode).toBe(200);
    expect(fetchHtmlMock).toHaveBeenCalledWith(50.0647, 19.945);
    expect(JSON.parse(response.body)).toMatchObject({
      configurationId: "krakow-home",
      coordinates: { latitude: 50.0647, longitude: 19.945 }
    });
  });

  test("accepts direct coordinates", async () => {
    fetchHtmlMock.mockResolvedValue("html");
    parseMock.mockReturnValue([]);

    const response = await handler({ body: JSON.stringify({ latitude: 27.9, longitude: 34.3 }) });

    expect(response.statusCode).toBe(200);
    expect(fetchHtmlMock).toHaveBeenCalledWith(27.9, 34.3);
  });

  test("rejects ambiguous or invalid input", async () => {
    expect((await handler({ body: JSON.stringify({}) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ configurationId: "missing" }) })).statusCode).toBe(404);
    expect((await handler({ body: JSON.stringify({ configurationId: "krakow-home", latitude: 1, longitude: 2 }) })).statusCode).toBe(400);
    expect((await handler({ body: JSON.stringify({ latitude: 100, longitude: 2 }) })).statusCode).toBe(400);
  });
});