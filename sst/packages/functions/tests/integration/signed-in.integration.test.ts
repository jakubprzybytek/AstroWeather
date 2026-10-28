import { beforeAll, describe, expect, inject, test } from "vitest";

// The signed-in routes, called as the test user (group astroweather-test, accepted off prod only).
const BASE_URL = inject("apiUrl");
const user = inject("testUser");

// Plain-password sign-in, enabled on the non-prod app clients only.
async function idToken(): Promise<string> {
  const response = await fetch("https://cognito-idp.eu-west-1.amazonaws.com/", {
    method: "POST",
    headers: {
      "content-type": "application/x-amz-json-1.1",
      "x-amz-target": "AWSCognitoIdentityProviderService.InitiateAuth"
    },
    body: JSON.stringify({
      AuthFlow: "USER_PASSWORD_AUTH",
      ClientId: user.clientId,
      AuthParameters: { USERNAME: user.email, PASSWORD: user.password }
    })
  });
  if (!response.ok) throw new Error(`Test user sign-in failed: HTTP ${response.status}`);
  return (await response.json()).AuthenticationResult.IdToken;
}

async function get(path: string, token: string): Promise<Response> {
  for (let attempt = 0; ; attempt += 1) {
    const response = await fetch(`${BASE_URL}${path}`, { headers: { authorization: `Bearer ${token}` } });
    if (response.status !== 429 || attempt === 3) return response;
    await new Promise((resolve) => setTimeout(resolve, 1100));
  }
}

describe("signed-in routes", () => {
  let token: string;
  beforeAll(async () => {
    token = await idToken();
  });

  test("GET /configurations lists the configurations", async () => {
    const response = await get("/configurations", token);

    expect(response.status).toBe(200);
    expect(await response.json()).toEqual(expect.arrayContaining([expect.objectContaining({ id: "wroclaw" })]));
  });

  test("GET /astro/{configurationId} returns the forecast", async () => {
    const response = await get("/astro/wroclaw", token);

    expect(response.status).toBe(200);
    expect((await response.text()).split("\n")[0]).toBe("protocol=4");
  });
});
