import { describe, expect, test } from "vitest";
import { formatTime } from "./time";

describe("formatTime", () => {
  test("formats a timestamp in the supplied timezone", () => {
    expect(formatTime("2026-09-06T04:00:00.000Z", "Europe/Warsaw")).toMatch(/6:00|06:00/);
  });

  test("renders missing times as an em dash", () => {
    expect(formatTime(null, "Europe/Warsaw")).toBe("—");
  });
});
