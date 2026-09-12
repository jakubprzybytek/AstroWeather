export type AstroResponse = {
  configId: string;
  timezone: string;
  sun: {
    rise: string | null;
    set: string | null;
  };
  moon: {
    rise: string | null;
    set: string | null;
    alwaysUp: boolean;
    alwaysDown: boolean;
  };
};

export type ClearOutsideHour = {
  hour: number;
  timestampUtc: string;
  temperatureC: number | null;
  cloudCoverTotalPct: number | null;
  precipitationProbabilityPct: number | null;
  thunderstormRisk: boolean | null;
};

export type ClearOutsideNight = {
  nightId: string;
  hours: ClearOutsideHour[];
};

export type ClearOutsideResponse = {
  configurationId?: string;
  coordinates: {
    latitude: number;
    longitude: number;
  };
  nights: ClearOutsideNight[];
};
