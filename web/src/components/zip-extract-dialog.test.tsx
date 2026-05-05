import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";

import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import JSZip from "jszip";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { ParsedZipPreview } from "../lib/zip-upload";
import type { ExtractStrategy, UploadPreviewResponse } from "../lib/types";
import { ZipExtractDialog } from "./zip-extract-dialog";

function makePreview(overrides: Partial<ParsedZipPreview> = {}): ParsedZipPreview {
  return {
    archiveFileName: "archive.zip",
    commonRoot: "Root",
    entries: [
      { kind: "directory", path: "Root", zipObject: {} as unknown as JSZip.JSZipObject },
      { kind: "directory", path: "Root/Empty", zipObject: {} as unknown as JSZip.JSZipObject },
      { kind: "file", path: "Root/game.gba", zipObject: {} as unknown as JSZip.JSZipObject },
    ],
    totalDirectories: 2,
    totalFiles: 1,
    totalUncompressedBytes: 3,
    zipNameWithoutExtension: "archive",
    ...overrides,
  };
}

function renderDialog(overrides: Partial<{
  strategy: ExtractStrategy;
  preview: ParsedZipPreview;
  overwriteExisting: boolean;
  conflicts: UploadPreviewResponse | null;
  checking: boolean;
  onCancel: () => void;
  onConfirm: (options: { strategy: ExtractStrategy; overwriteExisting: boolean }) => void;
  onStrategyChange: (strategy: ExtractStrategy) => void;
  onOverwriteChange: (value: boolean) => void;
}> = {}) {
  const props = {
    checking: false,
    conflicts: null,
    onCancel: vi.fn(),
    onConfirm: vi.fn(),
    onOverwriteChange: vi.fn(),
    onStrategyChange: vi.fn(),
    overwriteExisting: false,
    preview: makePreview(),
    strategy: "extract-into-folder" as ExtractStrategy,
    ...overrides,
  };

  render(<ZipExtractDialog {...props} />);
  return props;
}

describe("ZipExtractDialog", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("renders the dialog with ZIP name and options", () => {
    renderDialog();

    expect(screen.getByText("Extract ZIP")).toBeTruthy();
    expect(screen.getByText("archive.zip")).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Extract here/ })).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Extract into folder/ })).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Preserve full archive path/ })).toBeTruthy();
  });

  it("pre-selects Extract into folder by default", () => {
    renderDialog();

    const extractHere = screen.getByRole("radio", { name: /Extract here/ }) as HTMLInputElement;
    const extractFolder = screen.getByRole("radio", { name: /Extract into folder/ }) as HTMLInputElement;

    expect(extractHere.checked).toBe(false);
    expect(extractFolder.checked).toBe(true);
  });

  it("calls onCancel when Cancel is clicked", () => {
    const onCancel = vi.fn();

    renderDialog({ onCancel });
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    expect(onCancel).toHaveBeenCalledOnce();
  });

  it("calls onConfirm with the selected options when Extract is clicked", () => {
    const onConfirm = vi.fn();

    renderDialog({ onConfirm });
    fireEvent.click(screen.getByRole("button", { name: "Extract" }));

    expect(onConfirm).toHaveBeenCalledWith({ strategy: "extract-into-folder", overwriteExisting: false });
  });

  it("reports strategy changes through the controlled callback", () => {
    const onStrategyChange = vi.fn();

    renderDialog({ onStrategyChange });
    fireEvent.click(screen.getByRole("radio", { name: /Extract here/ }));

    expect(onStrategyChange).toHaveBeenCalledWith("extract-here");
  });

  it("shows preview paths for all strategies", () => {
    renderDialog();

    expect(screen.getByText("Empty")).toBeTruthy();
    expect(screen.getByText("game.gba")).toBeTruthy();
    expect(screen.getByText("archive/Empty")).toBeTruthy();
    expect(screen.getByText("archive/game.gba")).toBeTruthy();
    expect(screen.getByText("Root/Empty")).toBeTruthy();
    expect(screen.getByText("Root/game.gba")).toBeTruthy();
  });

  it("hides later ZIP strategies that resolve to duplicate upload paths", () => {
    renderDialog({
      preview: makePreview({
        commonRoot: null,
        entries: [
          { kind: "directory", path: "Emus", zipObject: {} as unknown as JSZip.JSZipObject },
          { kind: "directory", path: "Emus/tg5040", zipObject: {} as unknown as JSZip.JSZipObject },
          { kind: "file", path: "Emus/tg5040/DOS.pak/default.cfg", zipObject: {} as unknown as JSZip.JSZipObject },
        ],
        totalDirectories: 2,
        totalFiles: 1,
        zipNameWithoutExtension: "minui-dosbox-pure-pak",
      }),
    });

    expect(screen.getByRole("radio", { name: /Extract here/ })).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Extract into folder/ })).toBeTruthy();
    expect(screen.queryByRole("radio", { name: /Preserve full archive path/ })).toBeNull();
  });

  it("keeps Preserve full archive path when it uploads to distinct paths", () => {
    renderDialog({
      preview: makePreview({
        archiveFileName: "Central.Scrutinizer.pakz",
        commonRoot: "Tools",
        entries: [
          { kind: "directory", path: "Tools", zipObject: {} as unknown as JSZip.JSZipObject },
          { kind: "directory", path: "Tools/my355", zipObject: {} as unknown as JSZip.JSZipObject },
          {
            kind: "file",
            path: "Tools/my355/Central Scrutinizer.pak/launch.sh",
            zipObject: {} as unknown as JSZip.JSZipObject,
          },
        ],
        totalDirectories: 2,
        totalFiles: 1,
        zipNameWithoutExtension: "Central.Scrutinizer",
      }),
    });

    expect(screen.getByRole("radio", { name: /Preserve full archive path/ })).toBeTruthy();
    expect(screen.getByText("Tools/my355/Central Scrutinizer.pak/launch.sh")).toBeTruthy();
  });

  it("canonicalizes a hidden duplicate strategy before confirming", () => {
    const onConfirm = vi.fn();

    renderDialog({
      onConfirm,
      preview: makePreview({
        commonRoot: null,
        entries: [
          { kind: "directory", path: "Emus", zipObject: {} as unknown as JSZip.JSZipObject },
          { kind: "file", path: "Emus/tg5040/DOS.pak/default.cfg", zipObject: {} as unknown as JSZip.JSZipObject },
        ],
        totalDirectories: 1,
        totalFiles: 1,
      }),
      strategy: "preserve-full-path",
    });

    const extractHere = screen.getByRole("radio", { name: /Extract here/ }) as HTMLInputElement;

    expect(extractHere.checked).toBe(true);
    expect(screen.queryByRole("radio", { name: /Preserve full archive path/ })).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: "Extract" }));

    expect(onConfirm).toHaveBeenCalledWith({ strategy: "extract-here", overwriteExisting: false });
  });

  it("uses the app's primary and secondary button colors", () => {
    renderDialog();

    const cancel = screen.getByRole("button", { name: "Cancel" });
    const extract = screen.getByRole("button", { name: "Extract" });

    expect(cancel.className).toContain("bg-[var(--panel-alt)]");
    expect(cancel.className).toContain("text-[var(--text)]");
    expect(extract.className).toContain("text-white");
    expect(extract.className).not.toContain("text-black");
  });

  it("does not leave production component buttons using black text on accent backgrounds", () => {
    const componentsDir = path.join(process.cwd(), "src/components");
    const sourceFiles = readdirSync(componentsDir).filter(
      (fileName) => fileName.endsWith(".tsx") && !fileName.endsWith(".test.tsx"),
    );

    for (const sourceFile of sourceFiles) {
      const source = readFileSync(path.join(componentsDir, sourceFile), "utf8");

      expect(source, sourceFile).not.toContain("text-black");
    }
  });

  it("shows conflict summaries and overwrite guidance", () => {
    renderDialog({
      conflicts: {
        overwriteableCount: 2,
        blockingCount: 1,
        overwriteable: [{ kind: "overwrite", path: "Tools/tg5040/Central Scrutinizer.pak/pak.json" }],
        blocking: [{ kind: "file-over-directory", path: "Tools/tg5040/Central Scrutinizer.pak" }],
      },
    });

    expect(screen.getByText(/Replaceable file conflicts/)).toBeTruthy();
    expect(screen.getByText(/Blocking type conflicts/)).toBeTruthy();
    expect(screen.getByText(/Enable overwrite to replace these existing files/)).toBeTruthy();
  });

  it("shows inline checking status and disables editing while checking preflight conflicts", () => {
    renderDialog({ checking: true });

    expect((screen.getByRole("button", { name: "Checking..." }) as HTMLButtonElement).disabled).toBe(true);
    expect(screen.getByText("Checking destination for conflicts...")).toBeTruthy();
    expect((screen.getByRole("radio", { name: /Extract here/ }) as HTMLInputElement).disabled).toBe(true);
    expect((screen.getByRole("checkbox", { name: /Allow overwriting existing files/ }) as HTMLInputElement).disabled).toBe(true);
  });
});
