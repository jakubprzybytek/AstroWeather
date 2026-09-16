import { configurations } from "./configurations";

export const handler = async () => ({
  statusCode: 200,
  headers: {
    "content-type": "application/json"
  },
  body: JSON.stringify(Object.entries(configurations).map(([id, configuration]) => ({
    id,
    label: configuration.label
  })))
});