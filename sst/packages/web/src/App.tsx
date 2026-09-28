import { useEffect, useRef, useState } from "react";
import { Alert, Button, Card, Container, Nav, Spinner } from "react-bootstrap";
import { fetchAstro, fetchConfigurations } from "./api";
import { AstroResults } from "./components/AstroResults";
import { auroraTools, type AuroraToolId } from "./auroraTools";
import { AuroraTool } from "./components/AuroraTools";
import { ConfigSelect } from "./components/ConfigSelect";
import type { AstroResponse, Configuration } from "./types";
import { ClearOutsideTool } from "./components/ClearOutsideTool";

type View = "main" | "clearoutside" | AuroraToolId;

const views: { id: View; label: string }[] = [
  { id: "main", label: "Home" },
  { id: "clearoutside", label: "Clearoutside" },
  ...auroraTools
];

export default function App() {
  const [selectedId, setSelectedId] = useState("wroclaw");
  const [data, setData] = useState<AstroResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const requestId = useRef(0);
  const [view, setView] = useState<View>("main");
  const [configurations, setConfigurations] = useState<Configuration[]>([]);
  const [configurationsLoading, setConfigurationsLoading] = useState(true);
  const [configurationsError, setConfigurationsError] = useState<string | null>(null);

  useEffect(() => {
    void fetchConfigurations()
      .then(setConfigurations)
      .catch((cause) => setConfigurationsError(cause instanceof Error ? cause.message : "Unable to load configurations"))
      .finally(() => setConfigurationsLoading(false));
  }, []);

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
        setError(cause instanceof Error ? cause.message : "Unable to load API response");
      }
    } finally {
      if (currentRequest === requestId.current) setLoading(false);
    }
  }

  return (
    <Container className="app-container py-5">
      <div className="app-tabs">
        <Nav variant="pills" className="app-tabs__nav flex-column" role="tablist" aria-label="Application sections">
          {views.map((entry) => (
            <Nav.Item key={entry.id}>
              <Nav.Link
                as="button"
                type="button"
                active={view === entry.id}
                role="tab"
                aria-selected={view === entry.id}
                aria-controls={`${entry.id}-panel`}
                id={`${entry.id}-tab`}
                onClick={() => setView(entry.id)}
              >
                {entry.label}
              </Nav.Link>
            </Nav.Item>
          ))}
        </Nav>
        <div className="app-tabs__panels">
          <div id={`${view}-panel`} role="tabpanel" aria-labelledby={`${view}-tab`}>
            {view === "main" && (
              <Card className="mx-auto" style={{ maxWidth: "42rem" }}>
                <Card.Body>
                  <Card.Title as="h1">AstroWeather</Card.Title>
                  <Card.Text>Check today&apos;s sun and moon times.</Card.Text>
                  <form onSubmit={(event) => { event.preventDefault(); void submit(); }}>
                    <ConfigSelect
                      value={selectedId}
                      onChange={setSelectedId}
                      configurations={configurations}
                      disabled={configurationsLoading}
                    />
                    <Button className="mt-3" type="submit" disabled={!selectedId || loading}>
                      {loading && <Spinner animation="border" size="sm" className="me-2" />}
                      {loading ? "Loading…" : "Submit"}
                    </Button>
                  </form>
                  {error && <Alert className="mt-4 mb-0" variant="danger">{error}</Alert>}
                  {configurationsError && <Alert className="mt-4 mb-0" variant="danger">{configurationsError}</Alert>}
                  {data && <AstroResults data={data} />}
                </Card.Body>
              </Card>
            )}
            {view === "clearoutside" && <ClearOutsideTool configurations={configurations} />}
            {view !== "main" && view !== "clearoutside" && <AuroraTool key={view} id={view} configurations={configurations} />}
          </div>
        </div>
      </div>
    </Container>
  );
}
