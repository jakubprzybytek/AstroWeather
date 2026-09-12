import { useRef, useState } from "react";
import { Alert, Button, Card, Container, Nav, Spinner } from "react-bootstrap";
import { fetchAstro } from "./api";
import { AstroResults } from "./components/AstroResults";
import { ConfigSelect } from "./components/ConfigSelect";
import type { AstroResponse } from "./types";
import { ClearOutsideTool } from "./components/ClearOutsideTool";

export default function App() {
  const [selectedId, setSelectedId] = useState("");
  const [data, setData] = useState<AstroResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const requestId = useRef(0);
  const [view, setView] = useState<"main" | "clearoutside">("main");

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
      <div className="app-tabs">
        <Nav variant="pills" className="app-tabs__nav flex-column" role="tablist" aria-label="Application sections">
          <Nav.Item>
            <Nav.Link
              as="button"
              type="button"
              active={view === "main"}
              role="tab"
              aria-selected={view === "main"}
              aria-controls="home-panel"
              id="home-tab"
              onClick={() => setView("main")}
            >
              Home
            </Nav.Link>
          </Nav.Item>
          <Nav.Item>
            <Nav.Link
              as="button"
              type="button"
              active={view === "clearoutside"}
              role="tab"
              aria-selected={view === "clearoutside"}
              aria-controls="clearoutside-panel"
              id="clearoutside-tab"
              onClick={() => setView("clearoutside")}
            >
              Clearoutside
            </Nav.Link>
          </Nav.Item>
        </Nav>
        <div className="app-tabs__panels">
          {view === "main" && <div id="home-panel" role="tabpanel" aria-labelledby="home-tab">
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
          </div>}
          {view === "clearoutside" && <div id="clearoutside-panel" role="tabpanel" aria-labelledby="clearoutside-tab">
            <ClearOutsideTool />
          </div>}
        </div>
      </div>
    </Container>
  );
}
