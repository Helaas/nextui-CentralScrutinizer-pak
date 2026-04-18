import { existsSync } from "node:fs";
import path from "node:path";

import { describe, expect, it } from "vitest";

describe("platform assets", () => {
  it("ships dedicated dashboard icons for every non-DOS mapping", () => {
    const iconNames = [
      "3DO",
      "A800",
      "C128",
      "C64",
      "CPC",
      "DC",
      "INTELLIVISION",
      "JAGUAR",
      "MSX",
      "N64",
      "NDS",
      "PICO8",
      "PSP",
      "RPGM",
      "SATURN",
      "SCUMMVM",
      "SUPERGRAFX",
      "TIC80",
      "VIC20",
    ];

    for (const icon of iconNames) {
      expect(existsSync(path.join(process.cwd(), "public", "platforms", `${icon}.svg`))).toBe(true);
    }
  });
});
