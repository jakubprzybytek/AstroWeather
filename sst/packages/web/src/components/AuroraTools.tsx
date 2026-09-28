import type { AuroraToolId } from "../auroraTools";
import type { Configuration, GfzHp60Values, NoaaKpValues, NoaaOutlookValues, OvationValues } from "../types";
import { NightSpanTable, type SpanRow } from "./NightSpanTable";
import { SourceTool } from "./SourceTool";

function index(value: number) {
  return value.toFixed(2);
}

function percent(value: number) {
  return `${Math.round(value * 100)}%`;
}

const gfzRows: SpanRow<GfzHp60Values>[] = [
  { label: "Hp60 median", cell: (span) => index(span.median) },
  { label: "Hp60 75th percentile", cell: (span) => index(span.quantile75) },
  { label: "Hp60 maximum", cell: (span) => index(span.maximum) },
  { label: "P(4–5)", cell: (span) => percent(span.prob4to5) },
  { label: "P(5–6)", cell: (span) => percent(span.prob5to6) },
  { label: "P(6–7)", cell: (span) => percent(span.prob6to7) },
  { label: "P(7–8)", cell: (span) => percent(span.prob7to8) },
  { label: "P(≥8)", cell: (span) => percent(span.probAtLeast8) }
];

const noaaKpRows: SpanRow<NoaaKpValues>[] = [
  { label: "Kp", cell: (span) => index(span.kp) },
  { label: "Status", cell: (span) => span.status },
  { label: "NOAA scale", cell: (span) => span.noaaScale ?? "—" }
];

const noaaOutlookRows: SpanRow<NoaaOutlookValues>[] = [
  { label: "Largest Kp", cell: (span) => span.largestKp },
  { label: "Ap", cell: (span) => span.ap },
  { label: "F10.7", cell: (span) => span.f107 }
];

const ovationRows: SpanRow<OvationValues>[] = [
  { label: "Aurora probability", cell: (span) => `${span.probabilityPct}%` },
  { label: "Grid cell", cell: (span) => `${span.cell.latitude}, ${span.cell.longitude}` }
];

export function AuroraTool({ id, configurations }: { id: AuroraToolId; configurations: Configuration[] }) {
  switch (id) {
    case "gfz-hp60":
      return (
        <SourceTool<GfzHp60Values>
          id={id}
          title="GFZ Hp60"
          description="Test the GFZ hourly Hp60 ensemble forecast (72 hours ahead)."
          configurations={configurations}
          renderResults={(data) => <NightSpanTable title="GFZ Hp60 forecast" data={data} rows={gfzRows} />}
        />
      );
    case "noaa-kp":
      return (
        <SourceTool<NoaaKpValues>
          id={id}
          title="NOAA Kp"
          description="Test the NOAA SWPC 3-day Kp forecast (three-hour bins)."
          configurations={configurations}
          renderResults={(data) => <NightSpanTable title="NOAA Kp forecast" data={data} rows={noaaKpRows} />}
        />
      );
    case "noaa-outlook":
      return (
        <SourceTool<NoaaOutlookValues>
          id={id}
          title="NOAA 27-day"
          description="Test the NOAA SWPC 27-day outlook (one largest Kp per UTC day)."
          configurations={configurations}
          renderResults={(data) => <NightSpanTable title="NOAA 27-day outlook" data={data} rows={noaaOutlookRows} />}
        />
      );
    case "ovation":
      return (
        <SourceTool<OvationValues>
          id={id}
          title="OVATION"
          description="Test the NOAA OVATION 30-minute aurora nowcast at the location's grid cell."
          configurations={configurations}
          renderResults={(data) => <NightSpanTable title="OVATION nowcast" data={data} rows={ovationRows} />}
        />
      );
  }
}
