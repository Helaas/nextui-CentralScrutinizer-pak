import { expect, test } from "@playwright/test";

test("pairs and lands on the dashboard", async ({ page }) => {
  await page.goto("http://127.0.0.1:8877/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();

  const primaryNav = page.getByLabel("Primary");

  await expect(primaryNav.getByRole("button", { name: "Library", current: "page" })).toBeVisible();
  await expect(primaryNav.getByRole("button", { name: "Tools" })).toBeVisible();
  await expect(primaryNav.getByRole("button", { name: "Files" })).toHaveCount(0);
  await expect(page.getByRole("heading", { name: "Library" })).toBeVisible();
  await expect(page.getByRole("button", { name: /Game Boy Advance/i })).toBeVisible();
});
