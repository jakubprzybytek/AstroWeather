import { OVATION_URL, loadOvation } from "../aurora/ovation";
import { createSourceToolHandler } from "./handler";

export const handler = createSourceToolHandler({
  url: OVATION_URL,
  failureMessage: "Unable to load OVATION data",
  load: (location) => loadOvation(location.latitude, location.longitude)
});
