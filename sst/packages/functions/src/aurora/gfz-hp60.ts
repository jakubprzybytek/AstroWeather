import { fetchSourceDocument } from "./fetch";
import { utcSpan, type Span } from "./spans";

export const GFZ_HP60_URL =
  "https://spaceweather.gfz.de/fileadmin/SW-Monitor/hp60_product_file_FORECAST_HP60_SWIFT_DRIVEN_LAST.json";

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

export type GfzHp60Span = Span<GfzHp60Values>;

// Column names in the GFZ ensemble product file, see its README.rst.
const COLUMNS = {
  time: "Time (UTC)",
  median: "median",
  quantile75: "0.75-quantile",
  maximum: "maximum",
  prob4to5: "prob 4-5",
  prob5to6: "prob 5-6",
  prob6to7: "prob 6-7",
  prob7to8: "prob 7-8",
  probAtLeast8: "prob >= 8"
} as const;

type Column = Record<string, unknown>;

function column(document: Record<string, unknown>, name: string): Column {
  const value = document[name];
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`GFZ Hp60 file is missing the ${name} column`);
  }
  return value as Column;
}

function number(values: Column, name: string, index: string): number {
  const value = values[index];
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(`GFZ Hp60 ${name} column has an invalid value at row ${index}`);
  }
  return value;
}

// The file stamps rows as `dd-mm-yyyy HH:MM` in UTC.
function parseTime(value: unknown, index: string): Date {
  const match = typeof value === "string" ? value.match(/^(\d{2})-(\d{2})-(\d{4}) (\d{2}):(\d{2})$/) : null;
  if (!match) {
    throw new Error(`GFZ Hp60 file has an invalid time at row ${index}`);
  }
  return new Date(Date.UTC(
    Number(match[3]), Number(match[2]) - 1, Number(match[1]),
    Number(match[4]), Number(match[5])
  ));
}

// Rows are stamped with the start of each Hp60 hour; the interval length is
// taken from the file itself so the sibling Hp30 and Kp products parse too.
export function parseGfzHp60(json: string): GfzHp60Span[] {
  let document: unknown;
  try {
    document = JSON.parse(json);
  } catch {
    throw new Error("GFZ Hp60 file is not valid JSON");
  }
  if (!document || typeof document !== "object" || Array.isArray(document)) {
    throw new Error("GFZ Hp60 file is not a column object");
  }

  const columns = Object.fromEntries(
    Object.entries(COLUMNS).map(([key, name]) => [key, column(document as Record<string, unknown>, name)])
  ) as Record<keyof typeof COLUMNS, Column>;

  const indexes = Object.keys(columns.time).sort((left, right) => Number(left) - Number(right));
  if (indexes.length < 2) {
    throw new Error(`GFZ Hp60 file has ${indexes.length} rows`);
  }

  const times = indexes.map((index) => parseTime(columns.time[index], index));
  const stepMs = times[1].getTime() - times[0].getTime();
  if (stepMs <= 0) {
    throw new Error("GFZ Hp60 rows are not in ascending time order");
  }

  return indexes.map((index, position) => ({
    ...utcSpan(times[position], stepMs),
    median: number(columns.median, COLUMNS.median, index),
    quantile75: number(columns.quantile75, COLUMNS.quantile75, index),
    maximum: number(columns.maximum, COLUMNS.maximum, index),
    prob4to5: number(columns.prob4to5, COLUMNS.prob4to5, index),
    prob5to6: number(columns.prob5to6, COLUMNS.prob5to6, index),
    prob6to7: number(columns.prob6to7, COLUMNS.prob6to7, index),
    prob7to8: number(columns.prob7to8, COLUMNS.prob7to8, index),
    probAtLeast8: number(columns.probAtLeast8, COLUMNS.probAtLeast8, index)
  }));
}

export async function loadGfzHp60() {
  const document = await fetchSourceDocument(GFZ_HP60_URL, "GFZ Hp60");
  return {
    spans: parseGfzHp60(document.body),
    lastModified: document.lastModified
  };
}
