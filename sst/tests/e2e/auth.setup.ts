import { expect, test as setup } from "@playwright/test";
import { Resource } from "sst";
import { STORAGE_STATE } from "../../playwright.config";

// Signs in once as the test user (docs/development.md) and saves the session. Never prints the user or password;
// this project runs with tracing off.
setup("sign in as the test user", async ({ page }) => {
  await page.goto("/");
  await page.getByLabel("Email").fill(Resource.TestUser.email);
  await page.getByLabel("Password", { exact: true }).fill(Resource.TestUserPassword.value);
  await page.getByRole("button", { name: "Sign in" }).click();
  await expect(page.getByRole("button", { name: "Sign out" })).toBeVisible();
  await page.context().storageState({ path: STORAGE_STATE });
});
