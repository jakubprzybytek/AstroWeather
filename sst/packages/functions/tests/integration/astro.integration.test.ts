const BASE_URL = process.env.API_URL ?? "http://localhost:3000";

type AstroResponse = {
  configId: string;
  timezone: string;
  sun: {
    rise: string | null;
    set: string | null;
  };
  moon: {
    rise: string | null;
    set: string | null;
    alwaysUp: boolean;
    alwaysDown: boolean;
  };
};

function isIsoStringOrNull(value: unknown): value is string | null {
  if (value === null) return true;
  if (typeof value !== "string") return false;
  return !Number.isNaN(Date.parse(value));
}

describe("GET /astro/{configId}", () => {
  test("returns 404 for an unknown configId", async () => {
    const response = await fetch(`${BASE_URL}/astro/unknown-place`);

    expect(response.status).toBe(404);

    const body = await response.json();
    expect(body).toEqual({ message: "Configuration not found" });
  });

  test("returns 200 with correct shape for krakow-home", async () => {
    const response = await fetch(`${BASE_URL}/astro/krakow-home`);

    expect(response.status).toBe(200);
    expect(response.headers.get("content-type")).toContain("application/json");

    const body: AstroResponse = await response.json();

    expect(body.configId).toBe("krakow-home");
    expect(body.timezone).toBe("Europe/Warsaw");

    expect(isIsoStringOrNull(body.sun.rise)).toBe(true);
    expect(isIsoStringOrNull(body.sun.set)).toBe(true);

    expect(isIsoStringOrNull(body.moon.rise)).toBe(true);
    expect(isIsoStringOrNull(body.moon.set)).toBe(true);
    expect(typeof body.moon.alwaysUp).toBe("boolean");
    expect(typeof body.moon.alwaysDown).toBe("boolean");
  });

  test("returns non-200 when configId path parameter is missing", async () => {
    const response = await fetch(`${BASE_URL}/astro/`);

    expect(response.status).not.toBe(200);
  });
});
