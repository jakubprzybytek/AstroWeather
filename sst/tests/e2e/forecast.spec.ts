import { expect, test } from "@playwright/test";

test("lists the configurations and shows a forecast", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "AstroWeather" })).toBeVisible();
  await expect(page.getByTestId("app-version")).toBeVisible();

  const submit = page.getByRole("button", { name: "Submit" });
  await expect(submit).toBeEnabled();
  await submit.click();

  await expect(page.getByText("API response")).toBeVisible();
  await expect(page.getByText("HTTP 200")).toBeVisible();
  await expect(page.locator("pre")).toContainText("protocol=4");
});
