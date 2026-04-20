import { expect, test } from "@playwright/test";

test("keeps the primary shell visible while folders change in the tools file browser", async ({ page }) => {
  await page.goto("http://127.0.0.1:8877/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();

  const primaryNav = page.getByLabel("Primary");
  const filesPath = page.getByRole("navigation", { name: "Files path" });

  await page.getByRole("button", { name: "Tools" }).click();
  await page.getByRole("button", { name: /File Browser/ }).click();
  await expect(primaryNav.getByRole("button", { name: "Tools", current: "page" })).toBeVisible();
  await expect(filesPath).toContainText("SD Card");

  await page.getByRole("button", { name: "Open Roms" }).click();
  await expect(primaryNav.getByRole("button", { name: "Tools", current: "page" })).toBeVisible();
  await expect(filesPath).toContainText("Roms");
  await expect(page.getByRole("button", { name: "Open Game Boy Advance (GBA)" })).toBeVisible();
});
