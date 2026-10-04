import { timingSafeEqual } from "node:crypto";
import { Resource } from "sst";

// Lambda authorizer for the device route. The ST67 module's HTTP client cannot
// add request headers, so the device sends its key as the `key` query
// parameter; API Gateway rejects requests without one before calling this.
// The secret may hold several comma-separated keys, so a new key can be
// added before the device switches to it and the old one removed after.

type AuthorizerEvent = {
  queryStringParameters?: Record<string, string | undefined>;
};

export function isValidKey(key: string | undefined, secret: string): boolean {
  if (!key) return false;
  const candidate = Buffer.from(key);
  return secret
    .split(",")
    .map((value) => Buffer.from(value.trim()))
    .some((expected) => expected.length > 0
      && expected.length === candidate.length
      && timingSafeEqual(expected, candidate));
}

export const handler = async (event: AuthorizerEvent) => ({
  isAuthorized: isValidKey(event.queryStringParameters?.key, Resource.DeviceApiKey.value)
});
