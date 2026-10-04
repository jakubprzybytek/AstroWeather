import "@testing-library/jest-dom/vitest";
import { cleanup } from "@testing-library/react";
import { afterEach, vi } from "vitest";

// The app is rendered without the Authenticator in tests; api.ts still asks
// Amplify for the session's access token.
vi.mock("aws-amplify/auth", () => ({
  fetchAuthSession: async () => ({ tokens: { accessToken: { toString: () => "test-token" } } })
}));

afterEach(() => cleanup());
