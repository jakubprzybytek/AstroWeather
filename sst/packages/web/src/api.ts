import { fetchAuthSession } from "aws-amplify/auth";
import type { AstroResponse, ClearOutsideResponse, Configuration, SourceResponse } from "./types";

const apiUrl = import.meta.env.VITE_API_URL ?? "http://localhost:3000";

// Every route needs the signed-in user's access token; Amplify refreshes it
// when it has expired.
async function authorized(init: { method?: string; headers?: Record<string, string>; body?: string } = {}) {
  const token = (await fetchAuthSession()).tokens?.accessToken?.toString();
  return { ...init, headers: { ...init.headers, ...(token ? { authorization: `Bearer ${token}` } : {}) } };
}

export async function fetchConfigurations(): Promise<Configuration[]> {
  const response = await fetch(`${apiUrl}/configurations`, await authorized());
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load configurations");
  }

  return body as Configuration[];
}

export async function fetchAstro(configId: string): Promise<AstroResponse> {
  const response = await fetch(`${apiUrl}/astro/${encodeURIComponent(configId)}`, await authorized());
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
  const response = await fetch(`${apiUrl}/tools/clearoutside`, await authorized({
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(input)
  }));
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : "Unable to load Clearoutside data");
  }

  return body as ClearOutsideResponse;
}

export type SourceToolInput =
  | { configurationId: string }
  | { latitude: number; longitude: number; timezone: string };

// The aurora source tools share one request and response shape; `tool` is
// the path segment after `/tools/`.
export async function fetchSourceTool<T>(tool: string, input: SourceToolInput): Promise<SourceResponse<T>> {
  const response = await fetch(`${apiUrl}/tools/${tool}`, await authorized({
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(input)
  }));
  const body = await response.json().catch(() => ({}));

  if (!response.ok) {
    throw new Error(typeof body.message === "string" ? body.message : `Unable to load ${tool} data`);
  }

  return body as SourceResponse<T>;
}
