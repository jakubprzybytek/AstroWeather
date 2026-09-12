import SunCalc from "suncalc";
import { configurations } from "./configurations";

type ConfigurationId = keyof typeof configurations;

function toIsoOrNull(value?: Date | null) {
  if (!value || Number.isNaN(value.getTime())) {
    return null;
  }

  return value.toISOString();
}

function isConfigurationId(value: string): value is ConfigurationId {
  return value in configurations;
}

export const handler = async (event: { pathParameters?: { configurationId?: string } }) => {
  const configurationId = event.pathParameters?.configurationId;

  if (!configurationId || !isConfigurationId(configurationId)) {
    return {
      statusCode: 404,
      headers: {
        "content-type": "application/json"
      },
      body: JSON.stringify({
        message: "Configuration not found"
      })
    };
  }

  const location = configurations[configurationId].location;
  const now = new Date();
  const sunTimes = SunCalc.getTimes(now, location.lat, location.lon);
  const moonTimes = SunCalc.getMoonTimes(now, location.lat, location.lon);

  return {
    statusCode: 200,
    headers: {
      "content-type": "application/json"
    },
    body: JSON.stringify({
      configurationId,
      timezone: location.tz,
      sun: {
        rise: toIsoOrNull(sunTimes.sunrise),
        set: toIsoOrNull(sunTimes.sunset)
      },
      moon: {
        rise: toIsoOrNull(moonTimes.rise),
        set: toIsoOrNull(moonTimes.set),
        alwaysUp: moonTimes.alwaysUp ?? false,
        alwaysDown: moonTimes.alwaysDown ?? false
      }
    })
  };
};
