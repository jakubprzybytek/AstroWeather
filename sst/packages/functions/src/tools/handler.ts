import { configurations } from "../configurations";
import { groupSpansByNight, type Span } from "../aurora/spans";

export type ToolLocation = {
  configurationId?: string;
  latitude: number;
  longitude: number;
  timezone: string;
};

export type ToolEvent = {
  body?: string | null;
};

type ToolRequest = {
  configurationId?: unknown;
  latitude?: unknown;
  longitude?: unknown;
  timezone?: unknown;
};

export type SourceLoad<T> = {
  spans: Span<T>[];
  lastModified: string | null;
  issuedAt?: string;
};

export type SourceToolOptions<T> = {
  url: string;
  failureMessage: string;
  load(location: ToolLocation): Promise<SourceLoad<T>>;
  now?: () => Date;
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

function isTimezone(value: unknown): value is string {
  if (typeof value !== "string" || value.length === 0) return false;
  try {
    new Intl.DateTimeFormat("en-CA", { timeZone: value });
    return true;
  } catch {
    return false;
  }
}

// The global sources ignore the coordinates, but every tool needs the timezone
// to cut spans into local nights, so a configuration or coordinates plus a
// timezone is required either way.
function resolveLocation(event: ToolEvent): ToolLocation | ReturnType<typeof jsonResponse> {
  let request: ToolRequest;
  try {
    request = JSON.parse(event.body ?? "{}");
  } catch {
    return jsonResponse(400, { message: "Request body must be valid JSON" });
  }

  const hasConfiguration = typeof request.configurationId === "string" && request.configurationId.length > 0;
  const hasCoordinates = request.latitude !== undefined || request.longitude !== undefined || request.timezone !== undefined;

  if (hasConfiguration === hasCoordinates) {
    return jsonResponse(400, { message: "Provide either configurationId or latitude, longitude and timezone" });
  }

  if (hasConfiguration) {
    const configurationId = request.configurationId as string;
    const configuration = configurations[configurationId as keyof typeof configurations];
    if (!configuration) {
      return jsonResponse(404, { message: "Configuration not found" });
    }
    return {
      configurationId,
      latitude: configuration.location.lat,
      longitude: configuration.location.lon,
      timezone: configuration.location.tz
    };
  }

  if (!isCoordinatePair(request.latitude, request.longitude)) {
    return jsonResponse(400, { message: "Latitude and longitude must be valid coordinates" });
  }
  if (!isTimezone(request.timezone)) {
    return jsonResponse(400, { message: "Timezone must be a valid IANA timezone name" });
  }
  return {
    latitude: request.latitude,
    longitude: request.longitude as number,
    timezone: request.timezone
  };
}

// Builds a `POST /tools/<source>` handler: resolves the location, loads the
// source, and returns its spans grouped by local night.
export function createSourceToolHandler<T>(options: SourceToolOptions<T>) {
  return async (event: ToolEvent) => {
    const location = resolveLocation(event);
    if ("statusCode" in location) {
      return location;
    }

    try {
      const loaded = await options.load(location);
      return jsonResponse(200, {
        configurationId: location.configurationId,
        coordinates: { latitude: location.latitude, longitude: location.longitude },
        timezone: location.timezone,
        source: {
          url: options.url,
          fetchedAt: (options.now ?? (() => new Date()))().toISOString(),
          lastModified: loaded.lastModified,
          issuedAt: loaded.issuedAt
        },
        nights: groupSpansByNight(loaded.spans, location.timezone)
      });
    } catch (cause) {
      return jsonResponse(502, {
        message: cause instanceof Error ? cause.message : options.failureMessage
      });
    }
  };
}
