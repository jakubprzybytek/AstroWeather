import { Fragment, type ReactNode } from "react";
import { Card, Table } from "react-bootstrap";
import type { SourceNight, SourceResponse, Span } from "../types";

export type SpanRow<T> = {
  label: string;
  cell(span: Span<T>): ReactNode;
};

const DAY_MS = 24 * 60 * 60 * 1000;

function timeIn(value: string, timezone: string) {
  return new Intl.DateTimeFormat("en-GB", { hour: "2-digit", minute: "2-digit", hourCycle: "h23", timeZone: timezone })
    .format(new Date(value));
}

function dateIn(value: string, timezone: string) {
  return new Intl.DateTimeFormat("en-GB", { month: "short", day: "2-digit", timeZone: timezone }).format(new Date(value));
}

// Spans keep the source's own boundaries, so the column label shows them in
// the location's local time: a day-long span by its start date, anything
// shorter by its start and end times.
function spanLabel(span: { start: string; end: string }, timezone: string) {
  if (Date.parse(span.end) - Date.parse(span.start) >= DAY_MS) {
    return `${dateIn(span.start, timezone)} ${timeIn(span.start, timezone)}–${dateIn(span.end, timezone)} ${timeIn(span.end, timezone)}`;
  }
  return `${timeIn(span.start, timezone)}–${timeIn(span.end, timezone)}`;
}

function formatInstant(value: string | null | undefined) {
  return value ? new Date(value).toISOString() : "—";
}

export type NightSpanTableProps<T> = {
  title: string;
  data: SourceResponse<T>;
  rows: SpanRow<T>[];
};

// Renders each night as its own header of spans followed by one row per
// metric, since the number and width of spans differ between nights.
export function NightSpanTable<T>({ title, data, rows }: NightSpanTableProps<T>) {
  return (
    <Card className="mt-4">
      <Card.Body>
        <Card.Title>{title}</Card.Title>
        <Card.Subtitle className="mb-3 text-muted">
          Resolved coordinates: {data.coordinates.latitude}, {data.coordinates.longitude} · Nights in {data.timezone}
        </Card.Subtitle>
        <Card.Text className="small text-muted">
          Source: <a href={data.source.url}>{data.source.url}</a><br />
          Fetched {formatInstant(data.source.fetchedAt)}
          {data.source.issuedAt && <> · Issued {formatInstant(data.source.issuedAt)}</>}
          {data.source.lastModified && <> · Last modified {data.source.lastModified}</>}
        </Card.Text>
        <Table responsive size="sm" className="mb-0">
          <tbody>
            {data.nights.map((night: SourceNight<T>) => (
              <Fragment key={night.nightId}>
                <tr>
                  <th scope="rowgroup">{night.nightId}</th>
                  {night.spans.map((span) => (
                    <th scope="col" key={span.start} title={`${span.start} – ${span.end}`}>{spanLabel(span, data.timezone)}</th>
                  ))}
                </tr>
                {rows.map((row) => (
                  <tr key={`${night.nightId}-${row.label}`}>
                    <th scope="row">{row.label}</th>
                    {night.spans.map((span) => <td key={span.start}>{row.cell(span)}</td>)}
                  </tr>
                ))}
              </Fragment>
            ))}
          </tbody>
        </Table>
      </Card.Body>
    </Card>
  );
}
