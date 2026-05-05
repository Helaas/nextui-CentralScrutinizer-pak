import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import JSZip from "jszip";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { ParsedZipPreview } from "../lib/zip-upload";
import { ZipExtractDialog } from "./zip-extract-dialog";

function makePreview(overrides: Partial<ParsedZipPreview> = {}): ParsedZipPreview {
  return {
    commonRoot: "Root",
    entries: [
      { kind: "directory", path: "Root", zipObject: {} as unknown as JSZip.JSZipObject },
      { kind: "directory", path: "Root/Empty", zipObject: {} as unknown as JSZip.JSZipObject },
      { kind: "file", path: "Root/game.gba", zipObject: {} as unknown as JSZip.JSZipObject },
    ],
    totalDirectories: 2,
    totalFiles: 1,
    zipNameWithoutExtension: "archive",
    ...overrides,
  };
}

describe("ZipExtractDialog", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("renders the dialog with ZIP name and options", () => {
    render(<ZipExtractDialog preview={makePreview()} onCancel={vi.fn()} onConfirm={vi.fn()} />);

    expect(screen.getByText("Extract ZIP")).toBeTruthy();
    expect(screen.getByText("archive.zip")).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Extract here/ })).toBeTruthy();
    expect(screen.getByRole("radio", { name: /Extract into folder/ })).toBeTruthy();
  });

  it("pre-selects Extract into folder by default", () => {
    render(<ZipExtractDialog preview={makePreview()} onCancel={vi.fn()} onConfirm={vi.fn()} />);

    const extractHere = screen.getByRole("radio", { name: /Extract here/ }) as HTMLInputElement;
    const extractFolder = screen.getByRole("radio", { name: /Extract into folder/ }) as HTMLInputElement;

    expect(extractHere.checked).toBe(false);
    expect(extractFolder.checked).toBe(true);
  });

  it("calls onCancel when Cancel is clicked", () => {
    const onCancel = vi.fn();

    render(<ZipExtractDialog preview={makePreview()} onCancel={onCancel} onConfirm={vi.fn()} />);
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    expect(onCancel).toHaveBeenCalledOnce();
  });

  it("calls onConfirm with selected strategy when Extract is clicked", () => {
    const onConfirm = vi.fn();

    render(<ZipExtractDialog preview={makePreview()} onCancel={vi.fn()} onConfirm={onConfirm} />);
    fireEvent.click(screen.getByRole("button", { name: "Extract" }));

    expect(onConfirm).toHaveBeenCalledWith("extract-into-folder");
  });

  it("switches strategy when a radio option is clicked", () => {
    const onConfirm = vi.fn();

    render(<ZipExtractDialog preview={makePreview()} onCancel={vi.fn()} onConfirm={onConfirm} />);
    fireEvent.click(screen.getByRole("radio", { name: /Extract here/ }));
    fireEvent.click(screen.getByRole("button", { name: "Extract" }));

    expect(onConfirm).toHaveBeenCalledWith("extract-here");
  });

  it("shows preview paths for both strategies", () => {
    render(<ZipExtractDialog preview={makePreview()} onCancel={vi.fn()} onConfirm={vi.fn()} />);

    expect(screen.getByText("Empty")).toBeTruthy();
    expect(screen.getByText("game.gba")).toBeTruthy();
    expect(screen.getByText("archive/Empty")).toBeTruthy();
    expect(screen.getByText("archive/game.gba")).toBeTruthy();
  });

  it("shows 'and N more' when there are more than 5 entries", () => {
    const entries = Array.from({ length: 7 }, (_, i) => ({
      kind: "file" as const,
      path: `Root/file${i}.txt`,
      zipObject: {} as unknown as JSZip.JSZipObject,
    }));

    render(
      <ZipExtractDialog
        preview={makePreview({ entries, totalFiles: 7, totalDirectories: 0 })}
        onCancel={vi.fn()}
        onConfirm={vi.fn()}
      />,
    );

    expect(screen.getAllByText("...and 2 more")).toHaveLength(2);
  });
});
