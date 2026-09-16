import { Form } from "react-bootstrap";
import type { Configuration } from "../types";

type ConfigSelectProps = {
  value: string;
  onChange: (value: string) => void;
  configurations: Configuration[];
  disabled?: boolean;
};

export function ConfigSelect({ value, onChange, configurations, disabled }: ConfigSelectProps) {
  return (
    <Form.Group controlId="location">
      <Form.Label>Location</Form.Label>
      <Form.Select value={value} onChange={(event) => onChange(event.target.value)} disabled={disabled}>
        <option value="">Choose a location</option>
        {configurations.map((configuration) => (
          <option key={configuration.id} value={configuration.id}>
            {configuration.label}
          </option>
        ))}
      </Form.Select>
    </Form.Group>
  );
}
