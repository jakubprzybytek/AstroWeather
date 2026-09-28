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

// A source value with the UTC interval it applies to, at the source's own
// granularity; `start` inclusive, `end` exclusive.
export type Span<T> = T & {
  start: string;
  end: string;
};

export type SourceNight<T> = {
  nightId: string;
  spans: Span<T>[];
};

export type SourceResponse<T> = {
  configurationId?: string;
  coordinates: {
    latitude: number;
    longitude: number;
  };
  timezone: string;
  source: {
    url: string;
    fetchedAt: string;
    lastModified: string | null;
    issuedAt?: string;
  };
  nights: SourceNight<T>[];
};

export type GfzHp60Values = {
  median: number;
  quantile75: number;
  maximum: number;
  prob4to5: number;
  prob5to6: number;
  prob6to7: number;
  prob7to8: number;
  probAtLeast8: number;
};

export type NoaaKpValues = {
  kp: number;
  status: "estimated" | "predicted";
  noaaScale: string | null;
};

export type NoaaOutlookValues = {
  largestKp: number;
  ap: number;
  f107: number;
};

export type OvationValues = {
  probabilityPct: number;
  cell: {
    latitude: number;
    longitude: number;
  };
};
