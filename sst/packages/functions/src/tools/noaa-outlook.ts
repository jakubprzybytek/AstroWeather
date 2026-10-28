import { withUserAccess } from "../auth/user-access";
import { NOAA_27_DAY_OUTLOOK_URL, loadNoaa27DayOutlook } from "../aurora/noaa-outlook";
import { createSourceToolHandler } from "./handler";

export const handler = withUserAccess(createSourceToolHandler({
  url: NOAA_27_DAY_OUTLOOK_URL,
  failureMessage: "Unable to load NOAA 27-day outlook data",
  load: () => loadNoaa27DayOutlook()
}));
