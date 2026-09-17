import { configurations } from "./configurations";
import { assembleForecast } from "./forecast/assemble";
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

    try {
      const displays = await assembleForecast(configurationId, configurations[configurationId].location, {
        now: dependencies.now ?? (() => new Date()),
        readWeather: dependencies.readWeather ?? createWeatherReader().read,
        log: dependencies.log ?? ((message, details) => console.log(message, details))
      });
      return textResponse(200, serializeForecast(configurationId, displays));
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
