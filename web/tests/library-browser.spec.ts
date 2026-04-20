import { expect, test } from "@playwright/test";

test("keeps the library browser compact, searchable, and ROM-only for folder actions", async ({ page }) => {
  await page.goto("/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();

  const primaryNav = page.getByLabel("Primary");
  await expect(primaryNav).toBeVisible();

  await page.getByRole("button", { name: /Game Boy Advance/i }).click();
  await page.getByRole("button", { name: "ROMs" }).click();

  await expect(page.getByRole("navigation", { name: "Library path" })).toBeVisible();
  await expect(page.getByPlaceholder("Search in current folder")).toBeVisible();
  await expect(page.getByRole("button", { name: "Upload Folder" })).toBeVisible();
  await expect(page.getByRole("button", { name: "New Folder" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Refresh" })).toBeVisible();
  await expect(page.getByRole("button", { name: "More actions for Pokemon Emerald Renamed.gba" })).toBeVisible();
  await expect(page.getByRole("checkbox")).toHaveCount(0);

  await page.getByRole("button", { name: "Back" }).click();
  await page.getByRole("button", { name: "Saves" }).click();

  await expect(page.getByPlaceholder("Search in current folder")).toBeVisible();
  await expect(page.getByRole("button", { name: "Upload File" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Refresh" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Upload Folder" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "New Folder" })).toHaveCount(0);
  await expect(page.getByRole("checkbox")).toHaveCount(0);
});
