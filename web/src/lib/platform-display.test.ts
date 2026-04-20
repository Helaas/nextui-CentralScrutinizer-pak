import { describe, expect, it } from "vitest";

import {
  createPlatformDisplayNames,
  filterPlatformGroups,
  flattenPlatformGroups,
  formatPlatformDescription,
} from "./platform-display";
import type { PlatformGroup } from "./types";

function supportedResources(overrides: Partial<Record<"roms" | "saves" | "states" | "bios" | "overlays" | "cheats", boolean>> = {}) {
  return {
    roms: true,
    saves: true,
    states: true,
    bios: true,
    overlays: true,
    cheats: true,
    ...overrides,
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

function buildGroups(): PlatformGroup[] {
  return [
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
          counts: { roms: 2, saves: 1, states: 0, bios: 0, overlays: 0, cheats: 0 },
        },
        {
          tag: "MGBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "MGBA",
          isCustom: false,
          ...emulatorState({ emulatorInstalled: false, emulatorWarning: "Missing MGBA emulator." }),
          romPath: "Roms/Game Boy Advance (MGBA)",
          savePath: "Saves/MGBA",
          biosPath: "Bios/MGBA",
          supportedResources: supportedResources(),
          counts: { roms: 0, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
        },
      ],
    },
    {
      name: "Atari",
      platforms: [
        {
          tag: "A5200",
          name: "Atari 5200",
          group: "Atari",
          icon: "ATARI5200",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Atari 5200 (A5200)",
          savePath: "Saves/A5200",
          biosPath: "Bios/A5200",
          supportedResources: supportedResources(),
          counts: { roms: 0, saves: 0, states: 0, bios: 1, overlays: 0, cheats: 0 },
        },
        {
          tag: "LYNX",
          name: "Atari Lynx",
          group: "Atari",
          icon: "LYNX",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Atari Lynx (LYNX)",
          savePath: "Saves/LYNX",
          biosPath: "Bios/LYNX",
          supportedResources: supportedResources(),
          counts: { roms: 0, saves: 2, states: 0, bios: 0, overlays: 1, cheats: 0 },
        },
      ],
    },
    {
      name: "Computer",
      platforms: [
        {
          tag: "PUAE",
          name: "Amiga",
          group: "Computer",
          icon: "AMIGA",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Amiga (PUAE)",
          savePath: "Saves/PUAE",
          biosPath: "Bios/PUAE",
          supportedResources: supportedResources(),
          counts: { roms: 0, saves: 0, states: 0, bios: 1, overlays: 0, cheats: 0 },
        },
      ],
    },
  ];
}

describe("platform-display", () => {
  it("adds tags when duplicate platform names are visible together", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", "all", true);
    const displayNames = createPlatformDisplayNames(flattenPlatformGroups(visibleGroups));

    expect(displayNames.get("GBA")).toBe("Game Boy Advance (GBA)");
    expect(displayNames.get("MGBA")).toBe("Game Boy Advance (MGBA)");
  });

  it("drops the suffix when an empty duplicate platform is hidden", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", "all", false);
    const displayNames = createPlatformDisplayNames(flattenPlatformGroups(visibleGroups));

    expect(flattenPlatformGroups(visibleGroups)).toHaveLength(4);
    expect(displayNames.get("GBA")).toBe("Game Boy Advance");
    expect(displayNames.has("MGBA")).toBe(false);
    expect(displayNames.get("A5200")).toBe("Atari 5200");
    expect(displayNames.get("LYNX")).toBe("Atari Lynx");
    expect(displayNames.get("PUAE")).toBe("Amiga");
  });

  it("keeps platforms with plain BIOS files when show empty is off", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", "all", false);
    const visibleTags = flattenPlatformGroups(visibleGroups).map((platform) => platform.tag);

    expect(visibleTags).toContain("PUAE");
  });

  it("matches searches against the visible duplicate label", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "game boy advance (mgba)", "all", true);
    const visiblePlatforms = flattenPlatformGroups(visibleGroups);

    expect(visiblePlatforms).toHaveLength(1);
    expect(visiblePlatforms[0]?.tag).toBe("MGBA");
  });

  it("formats descriptions using only supported resources", () => {
    const description = formatPlatformDescription({
      tag: "PORTS",
      name: "Ports",
      group: "PortMaster",
      icon: "PORTMASTER",
      isCustom: false,
      ...emulatorState({ requiresEmulator: false }),
      romPath: "Roms/Ports (PORTS)",
      savePath: "Saves/PORTS",
      biosPath: "Bios/PORTS",
      supportedResources: supportedResources({
        saves: false,
        states: false,
        bios: false,
        overlays: false,
        cheats: false,
      }),
      counts: { roms: 7, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
    });

    expect(description).toBe("7 ROMs");
  });

  it("hides missing-emulator consoles in installed-only mode", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", "installed", true);
    const visibleTags = flattenPlatformGroups(visibleGroups).map((platform) => platform.tag);

    expect(visibleTags).toContain("GBA");
    expect(visibleTags).not.toContain("MGBA");
  });
});
