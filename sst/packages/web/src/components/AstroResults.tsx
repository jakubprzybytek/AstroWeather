import { Card } from "react-bootstrap";
import type { AstroResponse } from "../types";

export function AstroResults({ data }: { data: AstroResponse }) {
  return (
    <Card className="mt-4">
      <Card.Body>
        <Card.Title>API response</Card.Title>
        <Card.Subtitle className="mb-3 text-muted">
          HTTP {data.status}{data.contentType && ` · ${data.contentType}`}
        </Card.Subtitle>
        <pre className="mb-0" style={{ overflowX: "auto", fontFamily: "monospace" }}>{data.body}</pre>
      </Card.Body>
    </Card>
  );
}
