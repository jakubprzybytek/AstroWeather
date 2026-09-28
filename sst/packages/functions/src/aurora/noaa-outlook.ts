import { fetchSourceDocument } from "./fetch";
import { utcSpan, type Span } from "./spans";

export const NOAA_27_DAY_OUTLOOK_URL = "https://services.swpc.noaa.gov/text/27-day-outlook.txt";

const ONE_DAY_MS = 24 * 60 * 60 * 1000;

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

export type NoaaOutlookValues = {
  largestKp: number;
  ap: number;
  f107: number;
};

export type NoaaOutlookSpan = Span<NoaaOutlookValues>;

export type NoaaOutlook = {
  issuedAt: string;
  spans: NoaaOutlookSpan[];
};

function monthIndex(name: string, line: string): number {
  const index = MONTHS.indexOf(name);
  if (index < 0) {
    throw new Error(`NOAA 27-day outlook has an invalid month in "${line}"`);
  }
  return index;
}

// `:Issued: 2026 Sep 28 0221 UTC`
function parseIssued(text: string): string {
  const match = text.match(/^:Issued:\s+(\d{4}) (\w{3}) (\d{2}) (\d{2})(\d{2}) UTC/m);
  if (!match) {
    throw new Error("NOAA 27-day outlook is missing the issued line");
  }
  return new Date(Date.UTC(
    Number(match[1]), monthIndex(match[2], match[0]), Number(match[3]),
    Number(match[4]), Number(match[5])
  )).toISOString();
}

// Each data line is a UTC day: `2026 Sep 28      98           5          2`
// with the F10.7 radio flux, the planetary A index and the largest Kp.
export function parseNoaa27DayOutlook(text: string): NoaaOutlook {
  const issuedAt = parseIssued(text);
  const spans = text.split(/\r?\n/).flatMap((line) => {
    const match = line.match(/^(\d{4}) (\w{3}) (\d{2})\s+(\d+)\s+(\d+)\s+(\d+)\s*$/);
    if (!match) return [];

    const day = new Date(Date.UTC(Number(match[1]), monthIndex(match[2], line), Number(match[3])));
    return [{
      ...utcSpan(day, ONE_DAY_MS),
      f107: Number(match[4]),
      ap: Number(match[5]),
      largestKp: Number(match[6])
    }];
  });

  if (spans.length === 0) {
    throw new Error("NOAA 27-day outlook contains no daily rows");
  }
  return { issuedAt, spans };
}

export async function loadNoaa27DayOutlook() {
  const document = await fetchSourceDocument(NOAA_27_DAY_OUTLOOK_URL, "NOAA 27-day outlook");
  return {
    ...parseNoaa27DayOutlook(document.body),
    lastModified: document.lastModified
  };
}
