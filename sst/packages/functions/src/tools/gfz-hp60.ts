import { GFZ_HP60_URL, loadGfzHp60 } from "../aurora/gfz-hp60";
import { createSourceToolHandler } from "./handler";

export const handler = createSourceToolHandler({
  url: GFZ_HP60_URL,
  failureMessage: "Unable to load GFZ Hp60 data",
  load: () => loadGfzHp60()
});
