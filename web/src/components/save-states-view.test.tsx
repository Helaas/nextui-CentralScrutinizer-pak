import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn(
    (scope: string, path: string, _tag?: string, _csrf?: string) =>
      `/api/download?scope=${scope}&path=${encodeURIComponent(path)}`,
  ),
  deleteItem: vi.fn(),
  getSaveStates: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

import { SaveStatesView } from "./save-states-view";

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

describe("SaveStatesView", () => {
  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
    vi.clearAllMocks();
  });

  it("loads and renders save-state entries with warnings", async () => {
    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 1,
      truncated: false,
      entries: [
        {
          id: "GBA-mGBA:Pokemon Emerald.gba:0",
          title: "Pokemon Emerald.gba",
          coreDir: "GBA-mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "MinUI",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp",
          downloadPaths: [
            ".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0",
            ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp",
          ],
          deletePaths: [
            ".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0",
            ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp",
          ],
          warnings: ["Matching .minui metadata was not found."],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    expect(await screen.findByText("Pokemon Emerald.gba")).toBeTruthy();
    expect(screen.getByText("Slot 1 · GBA-mGBA · MinUI")).toBeTruthy();
    expect(screen.getByText("Matching .minui metadata was not found.")).toBeTruthy();
    expect(mockApi.getSaveStates).toHaveBeenCalledWith("GBA", "csrf-token");
  });

  it("downloads the selected bundle as a zip", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer,
    });
    const createObjectURL = vi.fn(() => "blob:download");
    const revokeObjectURL = vi.fn();
    const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => {});

    vi.stubGlobal("fetch", fetchMock);
    vi.stubGlobal("URL", {
      ...URL,
      createObjectURL,
      revokeObjectURL,
    });

    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 1,
      truncated: false,
      entries: [
        {
          id: "state-1",
          title: "Pokemon Emerald.gba",
          coreDir: "GBA-mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "MinUI",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: "",
          downloadPaths: [
            ".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0",
            ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp",
          ],
          deletePaths: [".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0"],
          warnings: [],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    fireEvent.click(await screen.findByRole("button", { name: "Download" }));

    await waitFor(() => {
      expect(fetchMock).toHaveBeenCalledTimes(2);
    });
    expect(clickSpy).toHaveBeenCalled();
    expect(createObjectURL).toHaveBeenCalled();
    expect(revokeObjectURL).toHaveBeenCalledWith("blob:download");
  });

  it("deletes all bundled files and refreshes the list", async () => {
    vi.stubGlobal("confirm", vi.fn(() => true));

    mockApi.getSaveStates
      .mockResolvedValueOnce({
        platformTag: "GBA",
        platformName: "Game Boy Advance",
        emuCode: "GBA",
        count: 1,
        truncated: false,
        entries: [
          {
            id: "state-1",
            title: "Pokemon Emerald.gba",
            coreDir: "GBA-mGBA",
            slot: 0,
            slotLabel: "Slot 1",
            kind: "slot",
            format: "MinUI",
            modified: 1_700_000_000,
            size: 4096,
            previewPath: "",
            downloadPaths: [".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0"],
            deletePaths: [
              ".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0",
              ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp",
            ],
            warnings: [],
          },
        ],
      })
      .mockResolvedValueOnce({
        platformTag: "GBA",
        platformName: "Game Boy Advance",
        emuCode: "GBA",
        count: 0,
        truncated: false,
        entries: [],
      });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        onChanged={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    fireEvent.click(await screen.findByRole("button", { name: "Delete" }));

    await waitFor(() => {
      expect(mockApi.deleteItem).toHaveBeenCalledTimes(2);
    });
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(1, { scope: "files", path: ".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0" }, "csrf-token");
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(2, { scope: "files", path: ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp" }, "csrf-token");
    expect(await screen.findByText("No save states found for this platform.")).toBeTruthy();
  });

  it("shows a truncation warning when the result set is capped", async () => {
    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 300,
      truncated: true,
      entries: [
        {
          id: "state-1",
          title: "Pokemon Emerald.gba",
          coreDir: "GBA-mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "MinUI",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: "",
          downloadPaths: [".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0"],
          deletePaths: [".userdata/shared/GBA-mGBA/Pokemon Emerald.gba.st0"],
          warnings: [],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    expect(await screen.findByText("Showing 1 of 300 save-state bundles. Refresh after deleting listed entries to load the rest.")).toBeTruthy();
  });
});
