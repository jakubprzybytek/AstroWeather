import type { AstroResponse } from "./types";

const apiUrl = import.meta.env.VITE_API_URL ?? "http://localhost:3000";

export async function fetchAstro(configId: string): Promise<AstroResponse> {
  const response = await fetch(`${apiUrl}/astro/${encodeURIComponent(configId)}`);
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load astronomy data");
  }

  return body as AstroResponse;
}
