import { expect, test } from "@playwright/test";

test("preserves redesigned file workspace affordances while navigating folders", async ({ page }) => {
  await page.goto("http://127.0.0.1:8877/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();
  await page.goto("http://127.0.0.1:8877/?view=files");

  const filesPath = page.getByRole("navigation", { name: "Files path" });
  const search = page.getByPlaceholder("Search in current folder");

  await expect(filesPath).toBeVisible();
  await expect(search).toBeVisible();
  await expect(page.getByRole("button", { name: "SD Card" })).toBeVisible();
  await expect(page.getByRole("checkbox")).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Open Roms" })).toBeVisible();

  await search.fill("Roms");
  await expect(page.getByRole("button", { name: "Open Roms" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Saves" })).toHaveCount(0);
  await search.fill("");

  await page.getByRole("button", { name: "Open Roms" }).click();
  await expect(filesPath).toContainText("SD Card");
  await expect(filesPath).toContainText("Roms");
  await expect(page.getByRole("button", { name: "Go to parent folder" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Game Boy Advance (GBA)" })).toBeVisible();
});
