import SunCalc from "suncalc";

const LOCATIONS = {
  "krakow-home": {
    latitude: 50.0647,
    longitude: 19.945,
    timezone: "Europe/Warsaw"
  }
} as const;

type LocationId = keyof typeof LOCATIONS;

function toIsoOrNull(value?: Date | null) {
  if (!value || Number.isNaN(value.getTime())) {
    return null;
  }

  return value.toISOString();
}

function isLocationId(value: string): value is LocationId {
  return value in LOCATIONS;
}

export const handler = async (event: { pathParameters?: { configId?: string } }) => {
  const configId = event.pathParameters?.configId;

  if (!configId || !isLocationId(configId)) {
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

  const location = LOCATIONS[configId];
  const now = new Date();
  const sunTimes = SunCalc.getTimes(now, location.latitude, location.longitude);
  const moonTimes = SunCalc.getMoonTimes(now, location.latitude, location.longitude);

  return {
    statusCode: 200,
    headers: {
      "content-type": "application/json"
    },
    body: JSON.stringify({
      configId,
      timezone: location.timezone,
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
