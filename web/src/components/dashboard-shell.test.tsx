import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { DashboardShell } from "./dashboard-shell";
import type { PlatformGroup } from "../lib/types";

const noopHandlers = {
  onChangeEmuFilter: vi.fn(),
  onSelectPlatform: vi.fn(),
  onToggleShowEmpty: vi.fn(),
};

function makePlatformGroup(name: string, tag: string): PlatformGroup {
  return {
    name,
    platforms: [
      {
        tag,
        name: tag,
        group: name,
        icon: tag,
        isCustom: false,
        requiresEmulator: false,
        emulatorInstalled: true,
        emulatorWarning: null,
        romPath: `Roms/${tag}`,
        savePath: `Saves/${tag}`,
        biosPath: `Bios/${tag}`,
        supportedResources: {
          roms: true,
          saves: false,
          states: false,
          bios: false,
          overlays: false,
          cheats: false,
        },
        counts: { roms: 1, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
      },
    ],
  };
}

describe("DashboardShell", () => {
  afterEach(() => {
    cleanup();
  });

  it("renders the loading skeleton while groups are empty and loading", () => {
    render(
      <DashboardShell
        emuFilter="all"
        groups={[]}
        isLoading
        showEmptyPlatforms={false}
        {...noopHandlers}
      />,
    );

    expect(screen.getByText(/Loading platforms/i)).toBeTruthy();
  });

  it("renders the platform grid once groups have arrived", () => {
    render(
      <DashboardShell
        emuFilter="all"
        groups={[makePlatformGroup("Nintendo", "GBA")]}
        isLoading={false}
        showEmptyPlatforms={false}
        {...noopHandlers}
      />,
    );

    expect(screen.queryByText(/Loading platforms/i)).toBeNull();
    expect(screen.getByText("Nintendo")).toBeTruthy();
  });

  it("shows an incremental loading hint when groups arrive but loading continues", () => {
    render(
      <DashboardShell
        emuFilter="all"
        groups={[makePlatformGroup("Nintendo", "GBA")]}
        isLoading
        showEmptyPlatforms={false}
        {...noopHandlers}
      />,
    );

    expect(screen.getByText(/Loading more platforms/i)).toBeTruthy();
    expect(screen.getByText("Nintendo")).toBeTruthy();
  });
});
