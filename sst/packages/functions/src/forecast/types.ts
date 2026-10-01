import type { AuroraNight } from "../aurora/storage";
import type { ClearOutsideItem } from "../weather/clearoutside-storage";

export type MatrixValue = "?" | string;

export type ForecastDisplay = {
  display: number;
  board: "num4x4_matrix5x21";
  nightId: string;
  sunset: string;
  sunrise: string;
  sun: MatrixValue;
  moon: MatrixValue;
  cloud: MatrixValue;
  precipitation: MatrixValue;  // probability, `*` for a thunderstorm risk
  aurora: MatrixValue;         // level; blinking down where the nowcast says now
  maximumTemperature: string;
  minimumTemperature: string;
};

// The aurora feeds with a fetch time in the payload, as `AuroraNight` names them.
export type AuroraFeed = "gfz" | "noaa3" | "noaa27" | "ovation";

export type AssembledForecast = {
  displays: ForecastDisplay[];
  // Newest `fetchedAt` among the weather items used; absent when there were none.
  lastWeatherFetch?: Date;
  // Per feed, the newest `fetchedAt` among the aurora items read for the
  // nights served; a feed is absent when none of its items was there.
  lastAuroraFetch: Partial<Record<AuroraFeed, Date>>;
  // How often the device should refresh: hourly on a storm night, else six-hourly.
  refreshIntervalMinutes: 60 | 360;
};

export type ForecastDependencies = {
  now: () => Date;
  readWeather: (configurationId: string, nightIds: string[], now: Date) => Promise<Map<string, ClearOutsideItem>>;
  readAurora: (configurationId: string, nightIds: string[], now: Date) => Promise<Map<string, AuroraNight>>;
  log?: (message: string, details: Record<string, unknown>) => void;
};