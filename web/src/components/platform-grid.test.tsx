import { render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { PlatformGrid } from "./platform-grid";

function supportedResources() {
  return {
    roms: true,
    saves: true,
    states: true,
    bios: true,
    overlays: true,
    cheats: true,
  };
}

function emulatorState(
  overrides: Partial<{ requiresEmulator: boolean; emulatorInstalled: boolean; emulatorWarning: string | null }> = {},
) {
  return {
    requiresEmulator: true,
    emulatorInstalled: true,
    emulatorWarning: null,
    ...overrides,
  };
}

describe("PlatformGrid", () => {
  it("renders grouped platform cards", () => {
    render(
      <PlatformGrid
        groups={[
          {
            name: "Nintendo",
            platforms: [
              {
                tag: "GBA",
                name: "Game Boy Advance",
                group: "Nintendo",
                icon: "GBA",
                isCustom: false,
                ...emulatorState(),
                romPath: "Roms/Game Boy Advance (GBA)",
                savePath: "Saves/GBA",
                biosPath: "Bios/GBA",
                supportedResources: supportedResources(),
                counts: { roms: 3, saves: 1, states: 2, bios: 0, overlays: 0, cheats: 0 },
              },
            ],
          },
        ]}
        onSelect={vi.fn()}
      />,
    );

    expect(screen.getByText("Nintendo")).toBeTruthy();
    expect(screen.getByText("Game Boy Advance")).toBeTruthy();
    expect(screen.getByText(/3 ROMs/)).toBeTruthy();
  });

  it("disambiguates duplicate platform names with their tags", () => {
    render(
      <PlatformGrid
        groups={[
          {
            name: "Nintendo",
            platforms: [
              {
                tag: "GBA",
                name: "Game Boy Advance",
                group: "Nintendo",
                icon: "GBA",
                isCustom: false,
                ...emulatorState({ emulatorInstalled: false, emulatorWarning: "Missing GBA emulator." }),
                romPath: "Roms/Game Boy Advance (GBA)",
                savePath: "Saves/GBA",
                biosPath: "Bios/GBA",
                supportedResources: supportedResources(),
                counts: { roms: 3, saves: 1, states: 0, bios: 0, overlays: 0, cheats: 0 },
              },
              {
                tag: "MGBA",
                name: "Game Boy Advance",
                group: "Nintendo",
                icon: "MGBA",
                isCustom: false,
                ...emulatorState(),
                romPath: "Roms/Game Boy Advance (MGBA)",
                savePath: "Saves/MGBA",
                biosPath: "Bios/MGBA",
                supportedResources: supportedResources(),
                counts: { roms: 4, saves: 2, states: 1, bios: 0, overlays: 0, cheats: 0 },
              },
            ],
          },
        ]}
        onSelect={vi.fn()}
      />,
    );

    expect(screen.getByText("Game Boy Advance (GBA)")).toBeTruthy();
    expect(screen.getByText("Game Boy Advance (MGBA)")).toBeTruthy();
    expect(screen.getByText("Missing emulator")).toBeTruthy();
  });
});
