import { describe, expect, it } from "vitest";

import { createPlatformDisplayNames, filterPlatformGroups, flattenPlatformGroups } from "./platform-display";
import type { PlatformGroup } from "./types";

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
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          counts: { roms: 2, saves: 1, bios: 0, overlays: 0, cheats: 0 },
          bios: { required: 1, present: 0, satisfied: false },
        },
        {
          tag: "MGBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "MGBA",
          isCustom: false,
          romPath: "Roms/Game Boy Advance (MGBA)",
          savePath: "Saves/MGBA",
          biosPath: "Bios/MGBA",
          counts: { roms: 0, saves: 0, bios: 0, overlays: 0, cheats: 0 },
          bios: { required: 1, present: 0, satisfied: false },
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
          romPath: "Roms/Atari 5200 (A5200)",
          savePath: "Saves/A5200",
          biosPath: "Bios/A5200",
          counts: { roms: 0, saves: 0, bios: 1, overlays: 0, cheats: 0 },
          bios: { required: 1, present: 1, satisfied: true },
        },
        {
          tag: "LYNX",
          name: "Atari Lynx",
          group: "Atari",
          icon: "LYNX",
          isCustom: false,
          romPath: "Roms/Atari Lynx (LYNX)",
          savePath: "Saves/LYNX",
          biosPath: "Bios/LYNX",
          counts: { roms: 0, saves: 2, bios: 0, overlays: 1, cheats: 0 },
          bios: { required: 0, present: 0, satisfied: true },
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
          romPath: "Roms/Amiga (PUAE)",
          savePath: "Saves/PUAE",
          biosPath: "Bios/PUAE",
          counts: { roms: 0, saves: 0, bios: 1, overlays: 0, cheats: 0 },
          bios: { required: 0, present: 0, satisfied: true },
        },
      ],
    },
  ];
}

describe("platform-display", () => {
  it("adds tags when duplicate platform names are visible together", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", true);
    const displayNames = createPlatformDisplayNames(flattenPlatformGroups(visibleGroups));

    expect(displayNames.get("GBA")).toBe("Game Boy Advance (GBA)");
    expect(displayNames.get("MGBA")).toBe("Game Boy Advance (MGBA)");
  });

  it("drops the suffix when an empty duplicate platform is hidden", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", false);
    const displayNames = createPlatformDisplayNames(flattenPlatformGroups(visibleGroups));

    expect(flattenPlatformGroups(visibleGroups)).toHaveLength(3);
    expect(displayNames.get("GBA")).toBe("Game Boy Advance");
    expect(displayNames.has("MGBA")).toBe(false);
    expect(displayNames.get("A5200")).toBe("Atari 5200");
    expect(displayNames.get("LYNX")).toBe("Atari Lynx");
  });

  it("hides platforms with only unrecognized bios files when show empty is off", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "", false);
    const visibleTags = flattenPlatformGroups(visibleGroups).map((platform) => platform.tag);

    expect(visibleTags).not.toContain("PUAE");
  });

  it("matches searches against the visible duplicate label", () => {
    const visibleGroups = filterPlatformGroups(buildGroups(), "game boy advance (mgba)", true);
    const visiblePlatforms = flattenPlatformGroups(visibleGroups);

    expect(visiblePlatforms).toHaveLength(1);
    expect(visiblePlatforms[0]?.tag).toBe("MGBA");
  });
});
