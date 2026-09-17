export type Configuration = {
  id: string;
  label: string;
};

export type AstroResponse = {
  status: number;
  contentType: string;
  body: string;
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
