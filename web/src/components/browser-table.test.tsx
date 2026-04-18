import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { BrowserTable } from "./browser-table";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn((scope: string, path: string, tag?: string) => `/api/download?scope=${scope}&path=${path}&tag=${tag ?? ""}`),
}));

vi.mock("../lib/api", () => mockApi);

describe("BrowserTable", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("renders files as a flat file-manager table without legacy columns or secondary menus", () => {
    render(
      <BrowserTable
        entries={[
          {
            name: "DC",
            path: "Cheats/DC",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
          {
            name: "readme.txt",
            path: "Cheats/readme.txt",
            type: "file",
            size: 128,
            modified: 1_700_000_100,
            status: "",
            thumbnailPath: "",
          },
        ]}
        onNavigate={vi.fn()}
        scope="files"
      />,
    );

    expect(screen.getByText("Name")).toBeTruthy();
    expect(screen.getByText("Size")).toBeTruthy();
    expect(screen.getByText("Modified")).toBeTruthy();
    expect(screen.getByText("Action")).toBeTruthy();
    expect(screen.queryByText("Type")).toBeNull();
    expect(screen.queryByText("Select")).toBeNull();
    expect(screen.queryByRole("checkbox")).toBeNull();
    expect(screen.queryByText("Cheats/DC")).toBeNull();
    expect(screen.queryByText("Cheats/readme.txt")).toBeNull();
    expect(screen.getByRole("button", { name: "Open DC" })).toBeTruthy();
    expect(screen.getByRole("link", { name: "Download readme.txt" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Download readme.txt" })).toBeNull();
    expect(screen.queryByRole("button", { name: "More actions for readme.txt" })).toBeNull();
  });

  it("renders a parent .. row when onNavigateParent is provided and delegates on click", () => {
    const onNavigateParent = vi.fn();

    render(
      <BrowserTable
        entries={[
          {
            name: "DC",
            path: "Cheats/DC",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]}
        onNavigate={vi.fn()}
        onNavigateParent={onNavigateParent}
        scope="files"
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "Go to parent folder" }));

    expect(onNavigateParent).toHaveBeenCalledTimes(1);
  });

  it("does not render a parent .. row at the files root", () => {
    render(<BrowserTable entries={[]} onNavigate={vi.fn()} scope="files" />);

    expect(screen.queryByRole("button", { name: "Go to parent folder" })).toBeNull();
  });

  it("shows a Delete action per files row that dispatches onDelete", () => {
    const onDelete = vi.fn();
    const entry = {
      name: "Saves",
      path: "Saves",
      type: "directory" as const,
      size: 0,
      modified: 1_700_000_000,
      status: "",
      thumbnailPath: "",
    };

    render(<BrowserTable entries={[entry]} onDelete={onDelete} onNavigate={vi.fn()} scope="files" />);

    fireEvent.click(screen.getByRole("button", { name: "Delete Saves" }));

    expect(onDelete).toHaveBeenCalledWith(entry);
  });

  it("surfaces an Edit action only for plaintext files when onEdit is provided", () => {
    const onEdit = vi.fn();
    const textEntry = {
      name: "readme.txt",
      path: "readme.txt",
      type: "file" as const,
      size: 12,
      modified: 1_700_000_000,
      status: "",
      thumbnailPath: "",
    };
    const binaryEntry = {
      name: "rom.gba",
      path: "rom.gba",
      type: "file" as const,
      size: 1024,
      modified: 1_700_000_100,
      status: "",
      thumbnailPath: "",
    };
    const folderEntry = {
      name: "Saves",
      path: "Saves",
      type: "directory" as const,
      size: 0,
      modified: 1_700_000_200,
      status: "",
      thumbnailPath: "",
    };

    render(<BrowserTable entries={[textEntry, binaryEntry, folderEntry]} onEdit={onEdit} onNavigate={vi.fn()} scope="files" />);

    fireEvent.click(screen.getByRole("button", { name: "Edit readme.txt" }));

    expect(onEdit).toHaveBeenCalledWith(textEntry);
    expect(screen.queryByRole("button", { name: "Edit rom.gba" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Edit Saves" })).toBeNull();
  });

  it("disables files row actions while busy", () => {
    render(
      <BrowserTable
        busy
        entries={[
          {
            name: "Saves",
            path: "Saves",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]}
        onDelete={vi.fn()}
        onNavigate={vi.fn()}
        onNavigateParent={vi.fn()}
        scope="files"
      />,
    );

    expect(screen.getByRole("button", { name: "Open Saves" })).toHaveProperty("disabled", true);
    expect(screen.getByRole("button", { name: "Delete Saves" })).toHaveProperty("disabled", true);
    expect(screen.getByRole("button", { name: "Go to parent folder" })).toHaveProperty("disabled", true);
  });

  it("renders library rows as a flatter table with glyphs and row actions", () => {
    render(
      <BrowserTable
        entries={[
          {
            name: "Pokemon Emerald.gba",
            path: "Roms/Pokemon Emerald.gba",
            type: "rom",
            size: 1024,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: ".media/Pokemon Emerald.png",
          },
          {
            name: "Favorites",
            path: "Roms/Favorites",
            type: "directory",
            size: 0,
            modified: 1_700_000_100,
            status: "",
            thumbnailPath: "",
          },
        ]}
        onDelete={vi.fn()}
        onNavigate={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        scope="roms"
        tag="GBA"
      />,
    );

    expect(screen.getByText("Name")).toBeTruthy();
    expect(screen.getByText("Size")).toBeTruthy();
    expect(screen.getByText("Modified")).toBeTruthy();
    expect(screen.getByText("Action")).toBeTruthy();
    expect(screen.queryByText("Type")).toBeNull();
    expect(screen.queryByText("Select")).toBeNull();
    expect(screen.queryByRole("checkbox")).toBeNull();
    expect(screen.queryByAltText("Pokemon Emerald.gba")).toBeNull();
    expect(screen.getByText("ROM")).toBeTruthy();
    expect(screen.getByText("DIR")).toBeTruthy();
    expect(screen.getByRole("link", { name: "Download Pokemon Emerald.gba" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Open Favorites" })).toBeTruthy();
    expect(screen.getAllByRole("button", { name: /More actions for/ })).toHaveLength(2);
    expect(mockApi.buildDownloadUrl).toHaveBeenCalledWith("roms", "Roms/Pokemon Emerald.gba", "GBA", undefined);
  });

  it("shows Replace Art only for ROM rows in the library overflow menu", () => {
    render(
      <BrowserTable
        entries={[
          {
            name: "Pokemon Emerald.gba",
            path: "Roms/Pokemon Emerald.gba",
            type: "rom",
            size: 1024,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: ".media/Pokemon Emerald.png",
          },
        ]}
        onDelete={vi.fn()}
        onNavigate={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        scope="roms"
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "More actions for Pokemon Emerald.gba" }));

    expect(screen.getByRole("menuitem", { name: "Rename" })).toBeTruthy();
    expect(screen.getByRole("menuitem", { name: "Replace Art" })).toBeTruthy();
    expect(screen.getByRole("menuitem", { name: "Delete" })).toBeTruthy();
    expect(screen.getByRole("menu").closest("div.overflow-visible")).toBeTruthy();
  });

  it("omits Replace Art for non-ROM library scopes", () => {
    render(
      <BrowserTable
        entries={[
          {
            name: "Pokemon Emerald.sav",
            path: "Saves/Pokemon Emerald.sav",
            type: "save",
            size: 1024,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]}
        onDelete={vi.fn()}
        onNavigate={vi.fn()}
        onRename={vi.fn()}
        onReplaceArt={vi.fn()}
        scope="saves"
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: "More actions for Pokemon Emerald.sav" }));

    expect(screen.getByRole("menuitem", { name: "Rename" })).toBeTruthy();
    expect(screen.queryByRole("menuitem", { name: "Replace Art" })).toBeNull();
    expect(screen.getByRole("menuitem", { name: "Delete" })).toBeTruthy();
  });
});
