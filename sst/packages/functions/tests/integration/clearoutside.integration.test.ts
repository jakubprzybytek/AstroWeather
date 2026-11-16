import { describe, expect, test } from "vitest";
import {
  fetchClearOutsideHtml,
  parseClearOutside,
  type ClearOutsideNight
} from "../../src/weather/clearoutside.js";

const coordinates = {
  latitude: 51.1079,
  longitude: 17.0385
};

describe("Clear Outside live forecast for Wrocław, Poland", () => {
  test.skipIf(process.env.RUN_LIVE_SCRAPE !== "1")(
    "scrapes and prints three nights of hourly weather data, the first one partial",
    async () => {
      const html = await fetchClearOutsideHtml(coordinates.latitude, coordinates.longitude);
      const nights: ClearOutsideNight[] = parseClearOutside(html);
      const selectedNights = nights.slice(0, 3);

      expect(selectedNights).toHaveLength(3);
      // The page starts at the hour it is generated, so the first night is
      // partial unless it is fetched at noon.
      expect(selectedNights[0].hours.length).toBeGreaterThan(0);
      for (const night of selectedNights) {
        if (night !== selectedNights[0]) expect(night.hours).toHaveLength(24);
        console.log(`\nWrocław, Poland - Night ${night.nightId}`);
        console.table(night.hours);
      }
    },
    30000
  );
});
