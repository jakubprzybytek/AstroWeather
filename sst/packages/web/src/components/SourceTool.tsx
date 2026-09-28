import { useRef, useState, type ReactNode } from "react";
import { Alert, Button, Card, Form, Spinner } from "react-bootstrap";
import { fetchSourceTool, type SourceToolInput } from "../api";
import type { Configuration, SourceResponse } from "../types";

export type SourceToolProps<T> = {
  id: string;
  title: string;
  description: string;
  configurations: Configuration[];
  renderResults(data: SourceResponse<T>): ReactNode;
};

// One form for every aurora source tool: a configuration, or coordinates plus
// the timezone that cuts the source's spans into local nights.
export function SourceTool<T>({ id, title, description, configurations, renderResults }: SourceToolProps<T>) {
  const [mode, setMode] = useState<"configuration" | "coordinates">("configuration");
  const [configurationId, setConfigurationId] = useState("");
  // The first configuration is preselected once the list has loaded.
  const selectedConfigurationId = configurationId || (configurations[0]?.id ?? "");
  const [latitude, setLatitude] = useState("");
  const [longitude, setLongitude] = useState("");
  const [timezone, setTimezone] = useState("Europe/Warsaw");
  const [data, setData] = useState<SourceResponse<T> | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const requestId = useRef(0);

  async function submit() {
    if (mode === "configuration" && !selectedConfigurationId) return;
    let input: SourceToolInput;
    if (mode === "configuration") {
      input = { configurationId: selectedConfigurationId };
    } else {
      input = { latitude: Number(latitude), longitude: Number(longitude), timezone: timezone.trim() };
      if (!Number.isFinite(input.latitude) || !Number.isFinite(input.longitude)) {
        setError("Enter valid latitude and longitude values");
        return;
      }
      if (!input.timezone) {
        setError("Enter an IANA timezone name");
        return;
      }
    }

    const currentRequest = ++requestId.current;
    setLoading(true);
    setError(null);
    setData(null);
    try {
      const result = await fetchSourceTool<T>(id, input);
      if (currentRequest === requestId.current) setData(result);
    } catch (cause) {
      if (currentRequest === requestId.current) {
        setError(cause instanceof Error ? cause.message : `Unable to load ${title} data`);
      }
    } finally {
      if (currentRequest === requestId.current) setLoading(false);
    }
  }

  return (
    <Card className="mx-auto" style={{ maxWidth: "72rem" }}>
      <Card.Body>
        <Card.Title as="h1">{title}</Card.Title>
        <Card.Text>{description}</Card.Text>
        <Form onSubmit={(event) => { event.preventDefault(); void submit(); }}>
          <Form.Group className="mb-3" controlId={`${id}-input-mode`}>
            <Form.Label>Input</Form.Label>
            <Form.Select value={mode} onChange={(event) => setMode(event.target.value as typeof mode)}>
              <option value="configuration">Configuration</option>
              <option value="coordinates">Coordinates</option>
            </Form.Select>
          </Form.Group>
          {mode === "configuration" ? (
            <Form.Group controlId={`${id}-configuration`}>
              <Form.Label>Configuration</Form.Label>
              <Form.Select value={selectedConfigurationId} onChange={(event) => setConfigurationId(event.target.value)}>
                {configurations.map((configuration) => <option key={configuration.id} value={configuration.id}>{configuration.label}</option>)}
              </Form.Select>
            </Form.Group>
          ) : (
            <div className="d-flex gap-3">
              <Form.Group controlId={`${id}-latitude`} className="flex-fill">
                <Form.Label>Latitude</Form.Label>
                <Form.Control type="number" step="any" value={latitude} onChange={(event) => setLatitude(event.target.value)} />
              </Form.Group>
              <Form.Group controlId={`${id}-longitude`} className="flex-fill">
                <Form.Label>Longitude</Form.Label>
                <Form.Control type="number" step="any" value={longitude} onChange={(event) => setLongitude(event.target.value)} />
              </Form.Group>
              <Form.Group controlId={`${id}-timezone`} className="flex-fill">
                <Form.Label>Timezone</Form.Label>
                <Form.Control type="text" value={timezone} onChange={(event) => setTimezone(event.target.value)} />
              </Form.Group>
            </div>
          )}
          <Button className="mt-3" type="submit" disabled={loading || (mode === "configuration" && !selectedConfigurationId)}>
            {loading && <Spinner animation="border" size="sm" className="me-2" />}
            {loading ? "Loading…" : "Fetch forecast"}
          </Button>
        </Form>
        {error && <Alert className="mt-4 mb-0" variant="danger">{error}</Alert>}
        {data && renderResults(data)}
      </Card.Body>
    </Card>
  );
}
