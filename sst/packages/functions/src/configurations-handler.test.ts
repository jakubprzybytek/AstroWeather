import { describe, expect, test } from "vitest";
import { listConfigurations } from "./configurations-handler";

describe("configurations handler", () => {
  test("returns the public configuration list", async () => {
    const response = await listConfigurations();

    expect(response.statusCode).toBe(200);
    expect(response.headers["content-type"]).toBe("application/json");
    expect(JSON.parse(response.body)).toEqual([
      { id: "wroclaw", label: "Wrocław" },
      { id: "krakow", label: "Kraków" }
    ]);
  });
});