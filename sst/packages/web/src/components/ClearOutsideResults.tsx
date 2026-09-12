import { Card, Table } from "react-bootstrap";
import type { ClearOutsideResponse } from "../types";

function value(value: number | null, suffix = "") {
  return value === null ? "—" : `${value}${suffix}`;
}

export function ClearOutsideResults({ data }: { data: ClearOutsideResponse }) {
  const hours = data.nights[0]?.hours ?? [];

  return (
    <Card className="mt-4">
      <Card.Body>
        <Card.Title>Clearoutside forecast</Card.Title>
        <Card.Subtitle className="mb-3 text-muted">
          Resolved coordinates: {data.coordinates.latitude}, {data.coordinates.longitude}
        </Card.Subtitle>
        <Table responsive size="sm" className="mb-0">
          <thead>
            <tr>
              <th scope="col">Night</th>
              {hours.map((hour) => <th scope="col" key={hour.hour}>{String(hour.hour).padStart(2, "0")}:00</th>)}
            </tr>
          </thead>
          <tbody>
            {data.nights.map((night) => (
              <>
                <tr key={night.nightId}>
                  <th colSpan={hours.length + 1} scope="rowgroup">{night.nightId}</th>
                </tr>
                <tr>
                  <th scope="row">Temperature</th>
                  {night.hours.map((hour) => <td key={hour.hour}>{value(hour.temperatureC, "°C")}</td>)}
                </tr>
                <tr>
                  <th scope="row">Cloud coverage</th>
                  {night.hours.map((hour) => <td key={hour.hour}>{value(hour.cloudCoverTotalPct, "%")}</td>)}
                </tr>
                <tr>
                  <th scope="row">Rain chance</th>
                  {night.hours.map((hour) => <td key={hour.hour}>{value(hour.precipitationProbabilityPct, "%")}</td>)}
                </tr>
                <tr>
                  <th scope="row">Thunderstorm risk</th>
                  {night.hours.map((hour) => (
                    <td key={hour.hour} aria-label={hour.thunderstormRisk ? "Thunderstorm risk" : undefined}>
                      {hour.thunderstormRisk ? "⚡" : ""}
                    </td>
                  ))}
                </tr>
              </>
            ))}
          </tbody>
        </Table>
      </Card.Body>
    </Card>
  );
}