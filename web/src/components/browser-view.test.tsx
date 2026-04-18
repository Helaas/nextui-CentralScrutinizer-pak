import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { BrowserView } from "./browser-view";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn((scope: string, path: string, tag?: string) => `/api/download?scope=${scope}&path=${path}&tag=${tag ?? ""}`),
  createFolder: vi.fn(),
  deleteItem: vi.fn(),
  getBrowser: vi.fn(),
  getPlatforms: vi.fn(),
  getSession: vi.fn(),
  pairBrowser: vi.fn(),
  renameItem: vi.fn(),
  revokeBrowser: vi.fn(),
  uploadFiles: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

describe("BrowserView", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
    window.history.replaceState(null, "", "/");
  });

  it("renders bios requirements inside the library browser without selection UI", () => {
    render(
      <BrowserView
        notice="Uploaded 1 file."
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "bios",
          title: "BIOS - PlayStation",
          rootPath: "Bios/PS",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [
            {
              name: "scph1001.bin",
              path: "scph1001.bin",
              type: "bios",
              size: 512,
              modified: 1_700_000_000,
              status: "present",
              thumbnailPath: "",
            },
          ],
          biosSummary: { required: 1, present: 1, satisfied: true },
          biosRequirements: [
            {
              label: "System BIOS",
              fileName: "scph1001.bin",
              path: "PS/scph1001.bin",
              status: "present",
              required: true,
            },
          ],
        }}
        scope="bios"
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.getByText("Uploaded 1 file.")).toBeTruthy();
    expect(screen.getByText("BIOS Status")).toBeTruthy();
    expect(screen.getByRole("navigation", { name: "Library path" })).toBeTruthy();
    expect(screen.getByPlaceholderText("Search in current folder")).toBeTruthy();
    expect(screen.queryByRole("checkbox")).toBeNull();
    expect(screen.queryByText("Delete Selected")).toBeNull();
    expect(screen.getByRole("button", { name: "More actions for scph1001.bin" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Upload Folder" })).toBeNull();
    expect(screen.queryByRole("button", { name: "New Folder" })).toBeNull();
  });

  it("filters library browser entries using the local search value", () => {
    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "Roms/Game Boy Advance (GBA)",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.gba",
              path: "Pokemon Emerald.gba",
              type: "rom",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: ".media/Pokemon Emerald.png",
            },
            {
              name: "Metroid Fusion.gba",
              path: "Metroid Fusion.gba",
              type: "rom",
              size: 2048,
              modified: 1_700_000_100,
              status: "",
              thumbnailPath: ".media/Metroid Fusion.png",
            },
          ],
        }}
        scope="roms"
        search="Metroid"
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.getByRole("link", { name: "Download Metroid Fusion.gba" })).toBeTruthy();
    expect(screen.queryByRole("link", { name: "Download Pokemon Emerald.gba" })).toBeNull();
    expect(screen.getByText("2 items")).toBeTruthy();
  });

  it("does not duplicate the root path text in the library header at the scope root", () => {
    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [],
        }}
        scope="roms"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.getAllByText("fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)")).toHaveLength(1);
  });

  it("uses only the breadcrumb for nested library paths", () => {
    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)",
          path: ".media",
          breadcrumbs: [{ label: ".media", path: ".media" }],
          truncated: false,
          entries: [],
        }}
        scope="roms"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.queryByText("fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)/.media")).toBeNull();
    expect(screen.getByRole("navigation", { name: "Library path" })).toBeTruthy();
  });

  it("renders files with a compact toolbar instead of the workspace header card", () => {
    render(
      <BrowserView
        busy={false}
        canUploadFolder={true}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFolder={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "files",
          title: "Files",
          rootPath: "SD Card",
          path: "Imports",
          breadcrumbs: [{ label: "Imports", path: "Imports" }],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.gba",
              path: "Imports/Pokemon Emerald.gba",
              type: "file",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: "",
            },
          ],
        }}
        scope="files"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.queryByRole("heading", { name: "Imports" })).toBeNull();
    expect(screen.getByRole("navigation", { name: "Files path" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "SD Card" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Imports" })).toBeTruthy();
    expect(screen.getByText("1 item")).toBeTruthy();
    expect(screen.getByText("SD Card/Imports", { selector: "p.break-all" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Upload File" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Upload Folder" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "New Folder" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Refresh" })).toBeTruthy();
    expect(screen.getByPlaceholderText("Search in current folder")).toBeTruthy();
  });

  it("does not render checkboxes or a selection bar in files", () => {
    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "files",
          title: "Files",
          rootPath: "SD Card",
          path: "Imports",
          breadcrumbs: [{ label: "Imports", path: "Imports" }],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.gba",
              path: "Imports/Pokemon Emerald.gba",
              type: "file",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: "",
            },
          ],
        }}
        scope="files"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.queryByRole("checkbox")).toBeNull();
    expect(screen.queryByText("1 selected")).toBeNull();
    expect(screen.queryByRole("button", { name: "Delete Selected" })).toBeNull();
    expect(screen.queryByRole("button", { name: "More actions for Pokemon Emerald.gba" })).toBeNull();
    expect(screen.getByRole("button", { name: "Delete Pokemon Emerald.gba" })).toBeTruthy();
  });

  it("renders a parent .. row for files outside the root", () => {
    const onNavigate = vi.fn();

    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={onNavigate}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "files",
          title: "Files",
          rootPath: "SD Card",
          path: "Cheats/DC",
          breadcrumbs: [
            { label: "Cheats", path: "Cheats" },
            { label: "DC", path: "Cheats/DC" },
          ],
          truncated: false,
          entries: [],
        }}
        scope="files"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Go to parent folder" }));

    expect(onNavigate).toHaveBeenCalledWith("Cheats");
  });

  it("wraps long files footer paths instead of truncating them", () => {
    const longPath = "SD Card/Imports/Very/Long/Path/That/Should/Wrap/Without/Overflow/AlphaBetaGammaDeltaEpsilonZeta";

    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "files",
          title: "Files",
          rootPath: "SD Card",
          path: "Imports/Very/Long/Path/That/Should/Wrap/Without/Overflow/AlphaBetaGammaDeltaEpsilonZeta",
          breadcrumbs: [
            { label: "Imports", path: "Imports" },
            { label: "Very", path: "Imports/Very" },
          ],
          truncated: false,
          entries: [],
        }}
        scope="files"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    const footerPath = screen.getByText(longPath);

    expect(footerPath.className).toContain("break-all");
  });

  it("shows Upload Folder only when folder uploads are supported", () => {
    const props = {
      busy: false,
      notice: null,
      onBack: vi.fn(),
      onCreateFolder: vi.fn(),
      onDeleteSelection: vi.fn(),
      onNavigate: vi.fn(),
      onRefresh: vi.fn(),
      onRename: vi.fn(),
      onReplaceArt: vi.fn(),
      onSearchChange: vi.fn(),
      onUploadFolder: vi.fn(),
      onUploadFiles: vi.fn(),
      response: {
        scope: "files" as const,
        title: "Files",
        rootPath: "SD Card",
        path: "Imports",
        breadcrumbs: [{ label: "Imports", path: "Imports" }],
        truncated: false,
        entries: [],
      },
      scope: "files" as const,
      search: "",
      transfer: { active: false, label: "", progress: 0 },
    };
    const { rerender } = render(<BrowserView {...props} canUploadFolder={false} />);

    expect(screen.queryByRole("button", { name: "Upload Folder" })).toBeNull();

    rerender(<BrowserView {...props} canUploadFolder />);

    expect(screen.getByRole("button", { name: "Upload Folder" })).toBeTruthy();
  });

  it("renders library browser chrome with ROM-only folder actions and a search bar", () => {
    render(
      <BrowserView
        busy={false}
        canUploadFolder={true}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFolder={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "Roms/Game Boy Advance (GBA)",
          path: "Favorites",
          breadcrumbs: [{ label: "Favorites", path: "Favorites" }],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.gba",
              path: "Favorites/Pokemon Emerald.gba",
              type: "rom",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: ".media/Pokemon Emerald.png",
            },
          ],
        }}
        scope="roms"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.getByRole("navigation", { name: "Library path" })).toBeTruthy();
    expect(screen.getByRole("heading", { name: "Favorites" })).toBeTruthy();
    expect(screen.getByText("1 item")).toBeTruthy();
    expect(screen.getByRole("button", { name: "Upload File" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Upload Folder" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "New Folder" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Refresh" })).toBeTruthy();
    expect(screen.getByPlaceholderText("Search in current folder")).toBeTruthy();
    expect(screen.queryByText("Roms/Game Boy Advance (GBA)/Favorites")).toBeNull();
    expect(screen.queryByRole("checkbox")).toBeNull();
    expect(screen.queryByText("Delete Selected")).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: "More actions for Pokemon Emerald.gba" }));

    expect(screen.getByRole("menuitem", { name: "Rename" })).toBeTruthy();
    expect(screen.getByRole("menuitem", { name: "Replace Art" })).toBeTruthy();
    expect(screen.getByRole("menuitem", { name: "Delete" })).toBeTruthy();
  });

  it("hides folder operations for non-ROM library scopes", () => {
    render(
      <BrowserView
        busy={false}
        canUploadFolder={true}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFolder={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "saves",
          title: "Saves - Game Boy Advance",
          rootPath: "Saves/GBA",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.sav",
              path: "Pokemon Emerald.sav",
              type: "save",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: "",
            },
          ],
        }}
        scope="saves"
        search=""
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(screen.getByRole("button", { name: "Upload File" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Refresh" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Upload Folder" })).toBeNull();
    expect(screen.queryByRole("button", { name: "New Folder" })).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: "More actions for Pokemon Emerald.sav" }));

    expect(screen.getByRole("menuitem", { name: "Rename" })).toBeTruthy();
    expect(screen.queryByRole("menuitem", { name: "Replace Art" })).toBeNull();
    expect(screen.getByRole("menuitem", { name: "Delete" })).toBeTruthy();
  });

  it("passes the platform tag through to rom download urls", () => {
    render(
      <BrowserView
        busy={false}
        notice={null}
        onBack={vi.fn()}
        onCreateFolder={vi.fn()}
        onDeleteSelection={vi.fn()}
        onNavigate={vi.fn()}
        onRefresh={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        onSearchChange={vi.fn()}
        onUploadFiles={vi.fn()}
        response={{
          scope: "roms",
          title: "ROMs - Game Boy Advance",
          rootPath: "Roms/Game Boy Advance (GBA)",
          path: "",
          breadcrumbs: [],
          truncated: false,
          entries: [
            {
              name: "Pokemon Emerald.gba",
              path: "Pokemon Emerald.gba",
              type: "rom",
              size: 1024,
              modified: 1_700_000_000,
              status: "",
              thumbnailPath: ".media/Pokemon Emerald.png",
            },
          ],
        }}
        scope="roms"
        search=""
        tag="GBA"
        transfer={{ active: false, label: "", progress: 0 }}
      />,
    );

    expect(mockApi.buildDownloadUrl).toHaveBeenCalledWith("roms", "Pokemon Emerald.gba", "GBA", undefined);
  });
});
