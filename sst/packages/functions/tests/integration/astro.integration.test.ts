import { describe, test, expect } from "vitest";
import { Resource } from "sst";

const BASE_URL = Resource.AstroApi.url;
const displayKeys = [
  "display", "board", "nightId", "numeric_0", "numeric_1",
  "matrix_0", "matrix_1", "matrix_2", "matrix_3", "numeric_2", "numeric_3"
];

describe("GET /astro/{configurationId}", () => {
  test("returns the six-display text protocol for a known configuration", async () => {
    const response = await fetch(`${BASE_URL}/astro/krakow`);
    const body = await response.text();
    const lines = body.split("\n");

    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("text/plain");
    expect(lines[0]).toBe("protocol=1");
    expect(lines[1]).toBe("configurationId=krakow");
    expect(lines[2]).toBe("");
    expect(lines[3]).toBe("display=0");
    expect(body).toContain("\n\ndisplay=1");

    expect(lines.filter((line) => line.startsWith("display="))).toEqual([
      "display=0", "display=1", "display=2", "display=3", "display=4", "display=5"
    ]);
    for (let display = 0; display < 6; display += 1) {
      const start = lines.indexOf(`display=${display}`);
      expect(lines.slice(start, start + displayKeys.length).map((line) => line.split("=", 1)[0]))
        .toEqual(displayKeys);
      const matrices = lines.slice(start + 5, start + 9).map((line) => line.split("=", 2)[1]);
      expect(matrices.every((value) => value === "?" || /^[*.?]{21}$/.test(value))).toBe(true);
    }
  });

  test("returns the versioned error payload for an unknown configuration", async () => {
    const response = await fetch(`${BASE_URL}/astro/unknown-place`);

    expect(response.status).toBe(404);
    expect(response.headers.get("content-type")).toContain("text/plain");
    expect(await response.text()).toBe("protocol=1\nerror=configuration_not_found\n");
  });

  test("returns non-200 when configurationId path parameter is missing", async () => {
    const response = await fetch(`${BASE_URL}/astro/`);

    expect(response.status).not.toBe(200);
  });
});

describe("GET /configurations", () => {
  test("returns public configuration metadata", async () => {
    const response = await fetch(`${BASE_URL}/configurations`);

    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("application/json");
    expect(await response.json()).toEqual([
      { id: "wroclaw", label: "Wrocław" },
      { id: "krakow", label: "Kraków" }
    ]);
  });
});
