import { expect, test } from "@playwright/test";

test("uses the app title and exposes PortMaster as a ROM-only library with .ports access", async ({ page }) => {
  await page.goto("/");
  await expect(page).toHaveTitle("Central Scrutinizer");

  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();

  await expect(page.getByRole("heading", { level: 2, name: "PortMaster" })).toBeVisible();
  await expect(page.getByRole("button", { name: /0\) Search/ })).toHaveCount(0);

  await page.getByRole("button", { name: /Ports/i }).click();
  await expect(page.getByRole("button", { name: "ROMs" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Saves" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Save States" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "BIOS" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Overlays" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Cheats" })).toHaveCount(0);

  await page.getByRole("button", { name: "ROMs" }).click();
  await expect(page.getByRole("button", { name: "Open .ports" })).toBeVisible();
  await expect(page.getByRole("link", { name: "Download PokeMMO.sh" })).toBeVisible();

  await page.getByRole("button", { name: "Open .ports" }).click();
  await expect(page.getByRole("link", { name: "Download port.json" })).toBeVisible();
});
