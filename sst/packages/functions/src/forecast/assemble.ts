import { calculateAstronomy } from "./astronomy";
import { nextNightIds } from "./nights";
import { projectWeather } from "./weather-reader";
import type { AssembledForecast, ForecastDependencies, ForecastDisplay } from "./types";

const BOARD = "num4x4_matrix5x21" as const;

function emptyDisplay(display: number, nightId: string): ForecastDisplay {
  return {
    display, board: BOARD, nightId,
    sunset: "?", sunrise: "?", sun: "?", moon: "?", cloud: "?",
    thunderstorm: "?", maximumTemperature: "?", minimumTemperature: "?"
  };
}

export async function assembleForecast(
  configurationId: string,
  location: { lat: number; lon: number; tz: string },
  dependencies: ForecastDependencies
): Promise<AssembledForecast> {
  const now = dependencies.now();
  const nightIds = nextNightIds(now, location.tz);
  const displays = nightIds.map((nightId, display) => emptyDisplay(display, nightId));
  let lastWeatherFetch: Date | undefined;

  for (const display of displays) {
    try {
      Object.assign(display, calculateAstronomy(display.nightId, location));
    } catch (cause) {
      dependencies.log?.("Forecast astronomy failed", {
        configurationId,
        nightId: display.nightId,
        error: cause instanceof Error ? cause.message : String(cause),
        source: "astronomy"
      });
    }
  }

  try {
    const weather = await dependencies.readWeather(configurationId, nightIds, now);
    for (const display of displays) {
      const item = weather.get(display.nightId);
      if (!item) continue;
      Object.assign(display, projectWeather(item, location.tz));
      const fetchedAt = new Date(item.fetchedAt);
      if (!Number.isNaN(fetchedAt.getTime()) && (!lastWeatherFetch || fetchedAt > lastWeatherFetch)) {
        lastWeatherFetch = fetchedAt;
      }
    }
  } catch (cause) {
    dependencies.log?.("Forecast weather failed", {
      configurationId,
      nightIds,
      error: cause instanceof Error ? cause.message : String(cause),
      source: "weather"
    });
  }

  return { displays, lastWeatherFetch };
}