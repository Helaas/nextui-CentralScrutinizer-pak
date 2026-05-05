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
