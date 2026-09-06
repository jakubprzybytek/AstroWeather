import { Card, Table } from "react-bootstrap";
import type { AstroResponse } from "../types";
import { formatTime } from "../time";

export function AstroResults({ data }: { data: AstroResponse }) {
  const moonNote = data.moon.alwaysUp
    ? "The moon is always above the horizon."
    : data.moon.alwaysDown
      ? "The moon stays below the horizon today."
      : null;

  return (
    <Card className="mt-4">
      <Card.Body>
        <Card.Title>Astro data</Card.Title>
        <Card.Subtitle className="mb-3 text-muted">{data.timezone}</Card.Subtitle>
        <Table responsive>
          <tbody>
            <tr><th>Sunrise</th><td>{formatTime(data.sun.rise, data.timezone)}</td></tr>
            <tr><th>Sunset</th><td>{formatTime(data.sun.set, data.timezone)}</td></tr>
            <tr><th>Moonrise</th><td>{formatTime(data.moon.rise, data.timezone)}</td></tr>
            <tr><th>Moonset</th><td>{formatTime(data.moon.set, data.timezone)}</td></tr>
          </tbody>
        </Table>
        {moonNote && <Card.Text className="mb-0">{moonNote}</Card.Text>}
      </Card.Body>
    </Card>
  );
}
