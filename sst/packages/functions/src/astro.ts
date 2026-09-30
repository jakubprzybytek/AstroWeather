import { configurations } from "./configurations";
import { assembleForecast } from "./forecast/assemble";
import { DEMO_CONFIGURATION_ID, DEMO_TIMEZONE, demoForecast } from "./forecast/demo";
import { localDateTime, localDateTimeMillis, utcOffset } from "./forecast/nights";
import { serializeError, serializeForecast } from "./forecast/protocol";
import { createWeatherReader } from "./forecast/weather-reader";
import { createAuroraStore } from "./aurora/storage";

type ConfigurationId = keyof typeof configurations;

function isConfigurationId(value: string): value is ConfigurationId {
  return value in configurations;
}

type AstroEvent = { pathParameters?: { configurationId?: string } };

function textResponse(statusCode: number, body: string) {
  return { statusCode, headers: { "content-type": "text/plain; charset=utf-8" }, body };
}

export function createHandler(
  dependencies: {
    now?: () => Date;
    readWeather?: ReturnType<typeof createWeatherReader>["read"];
    readAurora?: ReturnType<typeof createAuroraStore>["readNights"];
    log?: (message: string, details: Record<string, unknown>) => void;
  } = {}
) {
  return async (event: AstroEvent) => {
    const configurationId = event.pathParameters?.configurationId;
    const now = dependencies.now ?? (() => new Date());

    if (configurationId === DEMO_CONFIGURATION_ID) {
      // Made-up forecast data; only the time is real, for the device's clock.
      const { displays, refreshIntervalMinutes } = demoForecast(now());
      const sentAt = now();
      const time = localDateTimeMillis(sentAt, DEMO_TIMEZONE) + utcOffset(sentAt, DEMO_TIMEZONE);
      return textResponse(200, serializeForecast(configurationId, time, "?", refreshIntervalMinutes, displays));
    }

    if (!configurationId || !isConfigurationId(configurationId)) {
      return textResponse(404, serializeError("configuration_not_found"));
    }

    const { location, aurora } = configurations[configurationId];

    try {
      const { displays, lastWeatherFetch, refreshIntervalMinutes } = await assembleForecast(configurationId, location, aurora, {
        now,
        readWeather: dependencies.readWeather ?? createWeatherReader().read,
        readAurora: dependencies.readAurora ?? createAuroraStore().readNights,
        log: dependencies.log ?? ((message, details) => console.log(message, details))
      });
      // Read the clock after assembly so the device's RTC sync is as close to sending as possible.
      const sentAt = now();
      const time = localDateTimeMillis(sentAt, location.tz) + utcOffset(sentAt, location.tz);
      const lastWeatherFetchTime = lastWeatherFetch
        ? localDateTime(lastWeatherFetch, location.tz) + utcOffset(lastWeatherFetch, location.tz)
        : "?";
      return textResponse(200, serializeForecast(configurationId, time, lastWeatherFetchTime, refreshIntervalMinutes, displays));
    } catch (cause) {
      dependencies.log?.("Forecast response failed", {
        configurationId,
        error: cause instanceof Error ? cause.message : String(cause),
        source: "forecast"
      });
      return textResponse(500, serializeError("forecast_unavailable"));
    }
  };
}

export const handler = createHandler();
