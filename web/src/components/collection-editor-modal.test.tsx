import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => ({
  getBrowser: vi.fn(),
  getPlatforms: vi.fn(),
  writeTextFile: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

import { CollectionEditorModal } from "./collection-editor-modal";

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

describe("CollectionEditorModal", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("loads ROMs from nested platform folders into the picker", async () => {
    mockApi.getPlatforms.mockResolvedValue({
      groups: [
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
              supportedResources: supportedResources(),
              counts: { roms: 2, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
            },
          ],
        },
      ],
    });
    mockApi.getBrowser.mockImplementation((_scope: string, _csrf: string, _tag: string, path?: string) => {
      if (!path) {
        return Promise.resolve({
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "Roms/Game Boy Advance (GBA)",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [
            {
              name: "RPG",
              path: "RPG",
              type: "directory",
              size: 0,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: "",
            },
            {
              name: "Root Game.gba",
              path: "Root Game.gba",
              type: "rom",
              size: 1024,
              modified: 1_700_000_001,
              status: "",
              thumbnailPath: "",
            },
          ],
        });
      }

      expect(path).toBe("RPG");
      return Promise.resolve({
        scope: "roms",
        title: "ROMs - Game Boy Advance",
        rootPath: "Roms/Game Boy Advance (GBA)",
        path,
        breadcrumbs: [{ label: "RPG", path: "RPG" }],
        truncated: false,
        entries: [
          {
            name: "Nested Adventure.gba",
            path: "RPG/Nested Adventure.gba",
            type: "rom",
            size: 2048,
            modified: 1_700_000_002,
            status: "",
            thumbnailPath: "",
          },
        ],
      });
    });

    render(
      <CollectionEditorModal
        collection={{ fileName: "favorites.txt", name: "Favorites", romPaths: [] }}
        csrf="csrf-token"
        onClose={vi.fn()}
        onSaved={vi.fn()}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Add ROMs" }));

    expect(await screen.findByText("Root Game.gba")).toBeTruthy();
    expect(await screen.findByText("Nested Adventure.gba")).toBeTruthy();
    await waitFor(() => {
      expect(mockApi.getBrowser).toHaveBeenCalledWith("roms", "csrf-token", "GBA", "RPG");
    });
  });
});
