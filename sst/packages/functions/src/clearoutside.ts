import { configurations } from "./configurations";
import { fetchClearOutsideHtml, parseClearOutside } from "./weather/clearoutside";

type ClearOutsideRequest = {
  latitude?: unknown;
  longitude?: unknown;
  configurationId?: unknown;
};

type ClearOutsideEvent = {
  body?: string | null;
};

function jsonResponse(statusCode: number, body: unknown) {
  return {
    statusCode,
    headers: {
      "content-type": "application/json"
    },
    body: JSON.stringify(body)
  };
}

function isCoordinatePair(latitude: unknown, longitude: unknown): latitude is number {
  return typeof latitude === "number" && Number.isFinite(latitude) && latitude >= -90 && latitude <= 90
    && typeof longitude === "number" && Number.isFinite(longitude) && longitude >= -180 && longitude <= 180;
}

export const handler = async (event: ClearOutsideEvent) => {
  let request: ClearOutsideRequest;
  try {
    request = JSON.parse(event.body ?? "{}");
  } catch {
    return jsonResponse(400, { message: "Request body must be valid JSON" });
  }

  const hasConfiguration = typeof request.configurationId === "string" && request.configurationId.length > 0;
  const hasCoordinates = request.latitude !== undefined || request.longitude !== undefined;

  if (hasConfiguration === hasCoordinates) {
    return jsonResponse(400, { message: "Provide either configurationId or latitude and longitude" });
  }

  let latitude: number;
  let longitude: number;
  let configurationId: string | undefined;

  if (hasConfiguration) {
    configurationId = request.configurationId as string;
    const configuration = configurations[configurationId as keyof typeof configurations];
    if (!configuration) {
      return jsonResponse(404, { message: "Configuration not found" });
    }
    latitude = configuration.location.lat;
    longitude = configuration.location.lon;
  } else {
    if (!isCoordinatePair(request.latitude, request.longitude)) {
      return jsonResponse(400, { message: "Latitude and longitude must be valid coordinates" });
    }
    latitude = request.latitude;
    longitude = request.longitude as number;
  }

  try {
    const html = await fetchClearOutsideHtml(latitude, longitude);
    return jsonResponse(200, {
      configurationId,
      coordinates: { latitude, longitude },
      nights: parseClearOutside(html)
    });
  } catch (cause) {
    return jsonResponse(502, {
      message: cause instanceof Error ? cause.message : "Unable to load Clearoutside data"
    });
  }
};