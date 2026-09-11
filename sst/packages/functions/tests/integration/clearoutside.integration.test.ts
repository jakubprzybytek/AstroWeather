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
    "scrapes and prints at least three nights of hourly weather data",
    async () => {
      const html = await fetchClearOutsideHtml(coordinates.latitude, coordinates.longitude);
      const nights: ClearOutsideNight[] = parseClearOutside(html);
      const selectedNights = nights.slice(0, 3);

      expect(selectedNights).toHaveLength(3);
      for (const night of selectedNights) {
        expect(night.hours).toHaveLength(24);
        console.log(`\nWrocław, Poland - Night ${night.nightId}`);
        console.table(night.hours);
      }
    },
    30000
  );
});
