export function formatTime(value: string | null, timezone: string) {
  if (!value) return "—";
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
    timeZone: timezone
  }).format(new Date(value));
}
