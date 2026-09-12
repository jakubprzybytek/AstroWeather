import { useRef, useState } from "react";
import { Alert, Button, Card, Form, Spinner } from "react-bootstrap";
import { fetchClearOutside, type ClearOutsideInput } from "../api";
import { locations } from "../locations";
import type { ClearOutsideResponse } from "../types";
import { ClearOutsideResults } from "./ClearOutsideResults";

export function ClearOutsideTool() {
  const [mode, setMode] = useState<"configuration" | "coordinates">("configuration");
  const [configurationId, setConfigurationId] = useState("");
  const [latitude, setLatitude] = useState("");
  const [longitude, setLongitude] = useState("");
  const [data, setData] = useState<ClearOutsideResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const requestId = useRef(0);

  async function submit() {
    if (mode === "configuration" && !configurationId) return;
    const input: ClearOutsideInput = mode === "configuration"
      ? { configurationId }
      : { latitude: Number(latitude), longitude: Number(longitude) };
    if (mode === "coordinates") {
      const coordinates = input as Extract<ClearOutsideInput, { latitude: number }>;
      if (!Number.isFinite(coordinates.latitude) || !Number.isFinite(coordinates.longitude)) {
        setError("Enter valid latitude and longitude values");
        return;
      }
    }

    const currentRequest = ++requestId.current;
    setLoading(true);
    setError(null);
    setData(null);
    try {
      const result = await fetchClearOutside(input);
      if (currentRequest === requestId.current) setData(result);
    } catch (cause) {
      if (currentRequest === requestId.current) {
        setError(cause instanceof Error ? cause.message : "Unable to load Clearoutside data");
      }
    } finally {
      if (currentRequest === requestId.current) setLoading(false);
    }
  }

  return (
    <Card className="mx-auto" style={{ maxWidth: "72rem" }}>
      <Card.Body>
        <Card.Title as="h1">Clearoutside</Card.Title>
        <Card.Text>Test Clearoutside hourly forecast fetching.</Card.Text>
        <Form onSubmit={(event) => { event.preventDefault(); void submit(); }}>
          <Form.Group className="mb-3" controlId="clearoutside-input-mode">
            <Form.Label>Input</Form.Label>
            <Form.Select value={mode} onChange={(event) => setMode(event.target.value as typeof mode)}>
              <option value="configuration">Configuration</option>
              <option value="coordinates">Coordinates</option>
            </Form.Select>
          </Form.Group>
          {mode === "configuration" ? (
            <Form.Group controlId="clearoutside-configuration">
              <Form.Label>Configuration</Form.Label>
              <Form.Select value={configurationId} onChange={(event) => setConfigurationId(event.target.value)}>
                <option value="">Choose a configuration</option>
                {locations.map((location) => <option key={location.id} value={location.id}>{location.label}</option>)}
              </Form.Select>
            </Form.Group>
          ) : (
            <div className="d-flex gap-3">
              <Form.Group controlId="clearoutside-latitude" className="flex-fill">
                <Form.Label>Latitude</Form.Label>
                <Form.Control type="number" step="any" value={latitude} onChange={(event) => setLatitude(event.target.value)} />
              </Form.Group>
              <Form.Group controlId="clearoutside-longitude" className="flex-fill">
                <Form.Label>Longitude</Form.Label>
                <Form.Control type="number" step="any" value={longitude} onChange={(event) => setLongitude(event.target.value)} />
              </Form.Group>
            </div>
          )}
          <Button className="mt-3" type="submit" disabled={loading || (mode === "configuration" && !configurationId)}>
            {loading && <Spinner animation="border" size="sm" className="me-2" />}
            {loading ? "Loading…" : "Fetch forecast"}
          </Button>
        </Form>
        {error && <Alert className="mt-4 mb-0" variant="danger">{error}</Alert>}
        {data && <ClearOutsideResults data={data} />}
      </Card.Body>
    </Card>
  );
}