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
  thunderstorm: MatrixValue;
  maximumTemperature: string;
  minimumTemperature: string;
};

export type ForecastDependencies = {
  now: () => Date;
  readWeather: (configurationId: string, nightIds: string[], now: Date) => Promise<Map<string, ClearOutsideItem>>;
  log?: (message: string, details: Record<string, unknown>) => void;
};