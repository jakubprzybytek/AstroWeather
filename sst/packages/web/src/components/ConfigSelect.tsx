import { Form } from "react-bootstrap";
import { locations } from "../locations";

type ConfigSelectProps = {
  value: string;
  onChange: (value: string) => void;
};

export function ConfigSelect({ value, onChange }: ConfigSelectProps) {
  return (
    <Form.Group controlId="location">
      <Form.Label>Location</Form.Label>
      <Form.Select value={value} onChange={(event) => onChange(event.target.value)}>
        <option value="">Choose a location</option>
        {locations.map((location) => (
          <option key={location.id} value={location.id}>
            {location.label}
          </option>
        ))}
      </Form.Select>
    </Form.Group>
  );
}
