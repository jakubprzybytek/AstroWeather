import { configurations } from "./configurations";
import { assembleForecast } from "./forecast/assemble";
import { localDateTime, localDateTimeMillis } from "./forecast/nights";
import { serializeError, serializeForecast } from "./forecast/protocol";
import { createWeatherReader } from "./forecast/weather-reader";

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
    log?: (message: string, details: Record<string, unknown>) => void;
  } = {}
) {
  return async (event: AstroEvent) => {
    const configurationId = event.pathParameters?.configurationId;

    if (!configurationId || !isConfigurationId(configurationId)) {
      return textResponse(404, serializeError("configuration_not_found"));
    }

    const now = dependencies.now ?? (() => new Date());
    const { location } = configurations[configurationId];

    try {
      const { displays, lastWeatherFetch } = await assembleForecast(configurationId, location, {
        now,
        readWeather: dependencies.readWeather ?? createWeatherReader().read,
        log: dependencies.log ?? ((message, details) => console.log(message, details))
      });
      // Read the clock after assembly so the device's RTC sync is as close to sending as possible.
      const time = localDateTimeMillis(now(), location.tz);
      const lastWeatherFetchTime = lastWeatherFetch ? localDateTime(lastWeatherFetch, location.tz) : "?";
      return textResponse(200, serializeForecast(configurationId, time, lastWeatherFetchTime, displays));
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
