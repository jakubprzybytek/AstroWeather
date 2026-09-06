import { useRef, useState } from "react";
import { Alert, Button, Card, Container, Spinner } from "react-bootstrap";
import { fetchAstro } from "./api";
import { AstroResults } from "./components/AstroResults";
import { ConfigSelect } from "./components/ConfigSelect";
import type { AstroResponse } from "./types";

export default function App() {
  const [selectedId, setSelectedId] = useState("");
  const [data, setData] = useState<AstroResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const requestId = useRef(0);

  async function submit() {
    const currentRequest = ++requestId.current;
    setLoading(true);
    setError(null);
    setData(null);
    try {
      const result = await fetchAstro(selectedId);
      if (currentRequest === requestId.current) setData(result);
    } catch (cause) {
      if (currentRequest === requestId.current) {
        setError(cause instanceof Error ? cause.message : "Unable to load astronomy data");
      }
    } finally {
      if (currentRequest === requestId.current) setLoading(false);
    }
  }

  return (
    <Container className="py-5">
      <Card className="mx-auto" style={{ maxWidth: "42rem" }}>
        <Card.Body>
          <Card.Title as="h1">AstroWeather</Card.Title>
          <Card.Text>Check today&apos;s sun and moon times.</Card.Text>
          <form onSubmit={(event) => { event.preventDefault(); void submit(); }}>
            <ConfigSelect value={selectedId} onChange={setSelectedId} />
            <Button className="mt-3" type="submit" disabled={!selectedId || loading}>
              {loading && <Spinner animation="border" size="sm" className="me-2" />}
              {loading ? "Loading…" : "Submit"}
            </Button>
          </form>
          {error && <Alert className="mt-4 mb-0" variant="danger">{error}</Alert>}
          {data && <AstroResults data={data} />}
        </Card.Body>
      </Card>
    </Container>
  );
}
