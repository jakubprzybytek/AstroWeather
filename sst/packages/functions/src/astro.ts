import SunCalc from "suncalc";

const LOCATIONS = {
  "krakow-home": {
    latitude: 50.0647,
    longitude: 19.945,
    timezone: "Europe/Warsaw"
  }
} as const;

function toIsoOrNull(value?: Date) {
  return value ? value.toISOString() : null;
}

export const handler = async (event: { pathParameters?: { configId?: string } }) => {
  const configId = event.pathParameters?.configId;

  if (!configId || !(configId in LOCATIONS)) {
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

  const location = LOCATIONS[configId as keyof typeof LOCATIONS];
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
        rise: sunTimes.sunrise.toISOString(),
        set: sunTimes.sunset.toISOString()
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
