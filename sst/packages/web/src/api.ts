import type { AstroResponse, ClearOutsideResponse, Configuration } from "./types";

const apiUrl = import.meta.env.VITE_API_URL ?? "http://localhost:3000";

export async function fetchConfigurations(): Promise<Configuration[]> {
  const response = await fetch(`${apiUrl}/configurations`);
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load configurations");
  }

  return body as Configuration[];
}

export async function fetchAstro(configId: string): Promise<AstroResponse> {
  const response = await fetch(`${apiUrl}/astro/${encodeURIComponent(configId)}`);
  return {
    status: response.status,
    contentType: response.headers.get("content-type") ?? "",
    body: await response.text()
  };
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
