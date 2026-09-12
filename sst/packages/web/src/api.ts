import type { AstroResponse, ClearOutsideResponse } from "./types";

const apiUrl = import.meta.env.VITE_API_URL ?? "http://localhost:3000";

export async function fetchAstro(configId: string): Promise<AstroResponse> {
  const response = await fetch(`${apiUrl}/astro/${encodeURIComponent(configId)}`);
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load astronomy data");
  }

  return body as AstroResponse;
}

export type ClearOutsideInput =
  | { configurationId: string }
  | { latitude: number; longitude: number };

export async function fetchClearOutside(input: ClearOutsideInput): Promise<ClearOutsideResponse> {
  const response = await fetch(`${apiUrl}/tools/clearoutside`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(input)
  });
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load Clearoutside data");
  }

  return body as ClearOutsideResponse;
}
