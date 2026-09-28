// The aurora source tools, in tab order; `id` is also the `/tools/<id>` path.
export const auroraTools = [
  { id: "gfz-hp60", label: "GFZ Hp60" },
  { id: "noaa-kp", label: "NOAA Kp" },
  { id: "noaa-outlook", label: "NOAA 27-day" },
  { id: "ovation", label: "OVATION" }
] as const;

export type AuroraToolId = typeof auroraTools[number]["id"];
