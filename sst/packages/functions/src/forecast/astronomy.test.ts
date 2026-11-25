import * as SunCalc from "suncalc";
import { describe, expect, test } from "vitest";
import { sunAltitudeDegrees } from "./astronomy";

describe("sunAltitudeDegrees", () => {
  // The geometric altitude must match the limits suncalc's own event times use,
  // or the sun row and the dusk and dawn times disagree.
  test.each([["sunset", -0.833], ["dusk", -6], ["nauticalDusk", -12], ["night", -18]] as const)(
    "is %s's limit at suncalc's %s time", (event, degrees) => {
      const times = SunCalc.getTimes(new Date("2026-10-19T10:00:00Z"), 50.0647, 19.945);
      expect(sunAltitudeDegrees(times[event]!, 50.0647, 19.945)).toBeCloseTo(degrees, 1);
    });
});
