import { withUserAccess } from "../auth/user-access";
import { NOAA_KP_FORECAST_URL, loadNoaaKpForecast } from "../aurora/noaa-kp";
import { createSourceToolHandler } from "./handler";

export const handler = withUserAccess(createSourceToolHandler({
  url: NOAA_KP_FORECAST_URL,
  failureMessage: "Unable to load NOAA Kp forecast data",
  load: () => loadNoaaKpForecast()
}));
