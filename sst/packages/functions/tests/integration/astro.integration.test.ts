import { describe, test, expect, inject } from "vitest";

const BASE_URL = inject("apiUrl");
const DEVICE_KEY = inject("deviceKey");
const deviceUrl = (path: string) => `${BASE_URL}/device${path}?key=${encodeURIComponent(DEVICE_KEY)}`;

// The stage allows one request per second, so a test that follows another
// closely may be throttled; retry those few times.
async function get(url: string, init?: RequestInit): Promise<Response> {
  for (let attempt = 0; ; attempt += 1) {
    const response = await fetch(url, init);
    if (response.status !== 429 || attempt === 3) return response;
    await new Promise((resolve) => setTimeout(resolve, 1100));
  }
}
const displayKeys = [
  "display", "board", "nightId", "numeric_0", "numeric_1",
  "matrix_0", "matrix_1", "matrix_2", "matrix_3", "matrix_4", "numeric_2", "numeric_3"
];

describe("GET /device/astro/{configurationId}", () => {
  test("returns the six-display text protocol for a known configuration", async () => {
    const response = await get(deviceUrl("/astro/krakow"));
    const body = await response.text();
    const lines = body.split("\n");

    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("text/plain");
    expect(lines[0]).toBe("protocol=3");
    expect(lines[1]).toBe("configurationId=krakow");
    // Krakow is UTC+1 or UTC+2; with the offset the value names an exact instant.
    expect(lines[2]).toMatch(/^time=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\+0[12]:00$/);
    const renderedAt = Date.parse(lines[2].slice(5));
    expect(Math.abs(renderedAt - Date.now())).toBeLessThan(60_000);
    // Weather is ingested every six hours, so a working deployment has a value.
    expect(lines[3]).toMatch(/^lastWeatherFetchTime=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+0[12]:00$/);
    // A feed can be `?`: OVATION is fetched only on storm nights, and a stage
    // may not have run every aurora job yet.
    const fetchTime = String.raw`(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+0[12]:00|\?)`;
    expect(lines.slice(4, 8)).toEqual([
      expect.stringMatching(new RegExp(`^lastGfzFetchTime=${fetchTime}$`)),
      expect.stringMatching(new RegExp(`^lastNoaaKpFetchTime=${fetchTime}$`)),
      expect.stringMatching(new RegExp(`^lastNoaaOutlookFetchTime=${fetchTime}$`)),
      expect.stringMatching(new RegExp(`^lastOvationFetchTime=${fetchTime}$`))
    ]);
    expect(lines[8]).toMatch(/^refreshIntervalMinutes=(60|360)$/);
    expect(lines[9]).toBe("");
    expect(lines[10]).toBe("display=0");
    expect(body).toContain("\n\ndisplay=1");

    expect(lines.filter((line) => line.startsWith("display="))).toEqual([
      "display=0", "display=1", "display=2", "display=3", "display=4", "display=5"
    ]);
    for (let display = 0; display < 6; display += 1) {
      const start = lines.indexOf(`display=${display}`);
      expect(lines.slice(start, start + displayKeys.length).map((line) => line.split("=", 1)[0]))
        .toEqual(displayKeys);
      const matrices = lines.slice(start + 5, start + 10).map((line) => line.split("=", 2)[1]);
      expect(matrices.every((value) => value === "?" || /^[0-3abc*?]{21}$/.test(value))).toBe(true);
    }
  });

  test("returns the versioned error payload for an unknown configuration", async () => {
    const response = await get(deviceUrl("/astro/unknown-place"));

    expect(response.status).toBe(404);
    expect(response.headers.get("content-type")).toContain("text/plain");
    expect(await response.text()).toBe("protocol=3\nerror=configuration_not_found\n");
  });

  test("returns non-200 when configurationId path parameter is missing", async () => {
    const response = await get(deviceUrl("/astro/"));

    expect(response.status).not.toBe(200);
  });

  test("rejects a request without a key", async () => {
    const response = await get(`${BASE_URL}/device/astro/krakow`);

    expect(response.status).toBe(401);
  });

  test("rejects a wrong key", async () => {
    const response = await get(`${BASE_URL}/device/astro/krakow?key=not-the-key`);

    expect(response.status).toBe(403);
  });
});

// The web UI's routes need a Cognito access token; these tests have none, so
// they check only that the routes are closed without one.
describe("routes for signed-in users", () => {
  test.each(["/configurations", "/astro/krakow"])("GET %s rejects a request without a token", async (path) => {
    const response = await get(`${BASE_URL}${path}`);

    expect(response.status).toBe(401);
  });

  test("rejects a token that is not a valid JWT", async () => {
    const response = await get(`${BASE_URL}/configurations`, { headers: { authorization: "Bearer not-a-jwt" } });

    expect(response.status).toBe(401);
  });
});
