import { NOAA_27_DAY_OUTLOOK_URL, loadNoaa27DayOutlook } from "../aurora/noaa-outlook";
import { createSourceToolHandler } from "./handler";

export const handler = createSourceToolHandler({
  url: NOAA_27_DAY_OUTLOOK_URL,
  failureMessage: "Unable to load NOAA 27-day outlook data",
  load: () => loadNoaa27DayOutlook()
});
