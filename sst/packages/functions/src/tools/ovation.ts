import { withUserAccess } from "../auth/user-access";
import { OVATION_URL, loadOvation } from "../aurora/ovation";
import { createSourceToolHandler } from "./handler";

export const handler = withUserAccess(createSourceToolHandler({
  url: OVATION_URL,
  failureMessage: "Unable to load OVATION data",
  load: (location) => loadOvation(location.latitude, location.longitude)
}));
