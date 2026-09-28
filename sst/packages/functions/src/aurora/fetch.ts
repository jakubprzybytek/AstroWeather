export type SourceDocument = {
  body: string;
  lastModified: string | null;
};

// Fetches a public forecast file with one retry on a server-side failure.
// The sources are keyless static files, so there is no rate-limit handling.
export async function fetchSourceDocument(url: string, name: string): Promise<SourceDocument> {
  const maxAttempts = 2;

  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15_000);
    let response: Response;

    try {
      response = await fetch(url, {
        headers: {
          "user-agent": "AstroWeather/0.1 (aurora forecast research)"
        },
        signal: controller.signal
      });
    } catch (cause) {
      clearTimeout(timeout);
      if (attempt === maxAttempts - 1) {
        throw cause;
      }

      await retryDelay(attempt);
      continue;
    }

    clearTimeout(timeout);

    if (response.ok) {
      return {
        body: await response.text(),
        lastModified: response.headers.get("last-modified")
      };
    }

    if (response.status < 500 || attempt === maxAttempts - 1) {
      throw new Error(`${name} request failed with HTTP ${response.status}`);
    }

    await retryDelay(attempt);
  }

  throw new Error(`${name} request failed`);
}

function retryDelay(attempt: number): Promise<void> {
  const baseDelay = Math.min(2_000, 250 * 2 ** attempt);

  return new Promise((resolve) => {
    setTimeout(resolve, baseDelay + Math.floor(Math.random() * 100));
  });
}
