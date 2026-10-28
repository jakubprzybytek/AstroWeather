import { withUserAccess } from "./auth/user-access";
import { configurations } from "./configurations";

export const listConfigurations = async () => ({
  statusCode: 200,
  headers: {
    "content-type": "application/json"
  },
  body: JSON.stringify(Object.entries(configurations).map(([id, configuration]) => ({
    id,
    label: configuration.label
  })))
});
export const handler = withUserAccess(listConfigurations);
