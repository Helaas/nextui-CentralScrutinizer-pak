import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { BROWSER_MOVE_DRAG_TYPE } from "../lib/drag-types";
import { collectDroppedFiles, collectDroppedUploadSelection, DropZone } from "./drop-zone";

class MockDataTransfer {
  items: { kind: string; type: string; getAsFile: () => File; webkitGetAsEntry?: () => null }[] = [];
  files: File[] = [];

  addFile(file: File) {
    this.items.push({ kind: "file", type: file.type, getAsFile: () => file });
    this.files.push(file);
  }
}

type MockFileEntry = {
  isDirectory: false;
  isFile: true;
  name: string;
  file: (cb: (value: File) => void) => void;
};

type MockDirectoryEntry = {
  createReader: () => { readEntries: (cb: (entries: MockEntry[]) => void) => void };
  isDirectory: true;
  isFile: false;
  name: string;
};

type MockEntry = MockFileEntry | MockDirectoryEntry;

function createFileEntry(file: File): MockFileEntry {
  return {
    isDirectory: false,
    isFile: true,
    name: file.name,
    file: (cb: (value: File) => void) => {
      cb(file);
    },
  };
}

function createDirectoryEntry(name: string, batches: MockEntry[][]): MockDirectoryEntry {
  return {
    createReader: () => {
      let index = 0;

      return {
        readEntries: (cb: (entries: MockEntry[]) => void) => {
          cb(batches[index] ?? []);
          index += 1;
        },
      };
    },
    isDirectory: true,
    isFile: false,
    name,
  };
}

describe("DropZone", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  function createDataTransfer(files: File[]) {
    const dt = new MockDataTransfer();

    for (const file of files) {
      dt.addFile(file);
    }

    return dt;
  }

  it("renders children without an overlay by default", () => {
    render(
      <DropZone onDrop={vi.fn()}>
        <p>Content</p>
      </DropZone>,
    );

    expect(screen.getByText("Content")).toBeTruthy();
    expect(screen.queryByText("Drop files here to upload")).toBeNull();
  });

  it("shows the overlay on drag enter and hides it on drag leave", () => {
    render(
      <DropZone onDrop={vi.fn()}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;

    fireEvent.dragEnter(zone, { dataTransfer: createDataTransfer([]) });
    expect(screen.getByText("Drop files here to upload")).toBeTruthy();

    fireEvent.dragLeave(zone, { dataTransfer: createDataTransfer([]) });
    expect(screen.queryByText("Drop files here to upload")).toBeNull();
  });

  it("handles nested drag enter/leave via the counter pattern", () => {
    render(
      <DropZone onDrop={vi.fn()}>
        <div>
          <p>Nested</p>
        </div>
      </DropZone>,
    );

    const zone = screen.getByText("Nested").closest("[class*='relative']")!;
    const child = screen.getByText("Nested");

    fireEvent.dragEnter(zone, { dataTransfer: createDataTransfer([]) });
    fireEvent.dragEnter(child, { dataTransfer: createDataTransfer([]) });
    expect(screen.getByText("Drop files here to upload")).toBeTruthy();

    fireEvent.dragLeave(child, { dataTransfer: createDataTransfer([]) });
    expect(screen.getByText("Drop files here to upload")).toBeTruthy();

    fireEvent.dragLeave(zone, { dataTransfer: createDataTransfer([]) });
    expect(screen.queryByText("Drop files here to upload")).toBeNull();
  });

  it("calls onDrop with files when files are dropped", async () => {
    const onDrop = vi.fn();

    render(
      <DropZone onDrop={onDrop}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;
    const file = new File(["hello"], "test.txt", { type: "text/plain" });

    fireEvent.drop(zone, { dataTransfer: createDataTransfer([file]) });

    await waitFor(() => {
      expect(onDrop).toHaveBeenCalledTimes(1);
    });
    expect(onDrop.mock.calls[0][0].files).toEqual([file]);
    expect(onDrop.mock.calls[0][0].directories).toEqual([]);
  });

  it("hides the overlay after a drop", async () => {
    const onDrop = vi.fn();

    render(
      <DropZone onDrop={onDrop}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;

    fireEvent.dragEnter(zone, { dataTransfer: createDataTransfer([]) });
    expect(screen.getByText("Drop files here to upload")).toBeTruthy();

    fireEvent.drop(zone, { dataTransfer: createDataTransfer([new File(["x"], "a.rom")]) });

    await waitFor(() => {
      expect(screen.queryByText("Drop files here to upload")).toBeNull();
    });
  });

  it("does not show the overlay or fire onDrop when disabled", async () => {
    const onDrop = vi.fn();

    render(
      <DropZone disabled onDrop={onDrop}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;

    fireEvent.dragEnter(zone, { dataTransfer: createDataTransfer([]) });
    expect(screen.queryByText("Drop files here to upload")).toBeNull();

    fireEvent.drop(zone, { dataTransfer: createDataTransfer([new File(["x"], "a.rom")]) });

    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(onDrop).not.toHaveBeenCalled();
  });

  it("ignores empty drops", async () => {
    const onDrop = vi.fn();

    render(
      <DropZone onDrop={onDrop}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;

    fireEvent.drop(zone, { dataTransfer: createDataTransfer([]) });

    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(onDrop).not.toHaveBeenCalled();
  });

  it("ignores configured internal drag types", async () => {
    const onDrop = vi.fn();
    const dataTransfer = {
      files: [],
      items: [],
      types: [BROWSER_MOVE_DRAG_TYPE],
    } as unknown as DataTransfer;

    render(
      <DropZone ignoredDragTypes={[BROWSER_MOVE_DRAG_TYPE]} onDrop={onDrop}>
        <p>Content</p>
      </DropZone>,
    );

    const zone = screen.getByText("Content").closest("[class*='relative']")!;

    fireEvent.dragEnter(zone, { dataTransfer });
    fireEvent.dragOver(zone, { dataTransfer });
    fireEvent.drop(zone, { dataTransfer });

    await new Promise((resolve) => setTimeout(resolve, 20));
    expect(screen.queryByText("Drop files here to upload")).toBeNull();
    expect(onDrop).not.toHaveBeenCalled();
  });
});

describe("collectDroppedFiles", () => {
  it("falls back to dataTransfer.files when webkitGetAsEntry is unavailable", async () => {
    const file = new File(["data"], "rom.gba", { type: "application/octet-stream" });
    const dt = {
      items: [{ kind: "file", getAsFile: () => file }],
      files: [file],
    } as unknown as DataTransfer;

    const result = await collectDroppedFiles(dt);

    expect(result).toHaveLength(1);
    expect(result[0].name).toBe("rom.gba");
  });

  it("reads every batch from chunked directory readers", async () => {
    const emerald = new File(["emerald"], "Pokemon Emerald.gba", { type: "application/octet-stream" });
    const metroid = new File(["metroid"], "Metroid Fusion.gba", { type: "application/octet-stream" });
    const dt = {
      items: [
        {
          kind: "file",
          webkitGetAsEntry: () =>
            createDirectoryEntry("Favorites", [
              [createFileEntry(emerald)],
              [createFileEntry(metroid)],
              [],
            ]),
        },
      ],
      files: [],
    } as unknown as DataTransfer;

    const result = await collectDroppedFiles(dt);

    expect(result).toHaveLength(2);
    expect(
      result.map((file) => (file as File & { webkitRelativePath?: string }).webkitRelativePath),
    ).toEqual([
      "Favorites/Pokemon Emerald.gba",
      "Favorites/Metroid Fusion.gba",
    ]);
  });

  it("returns empty directories from dropped directory trees", async () => {
    const emerald = new File(["emerald"], "Pokemon Emerald.gba", { type: "application/octet-stream" });
    const dt = {
      items: [
        {
          kind: "file",
          webkitGetAsEntry: () =>
            createDirectoryEntry("Favorites", [
              [
                createDirectoryEntry("Empty", [[]]),
                createDirectoryEntry("Nested", [[createDirectoryEntry("Leaf", [[]])], []]),
                createFileEntry(emerald),
              ],
              [],
            ]),
        },
      ],
      files: [],
    } as unknown as DataTransfer;

    const result = await collectDroppedUploadSelection(dt);

    expect(result.directories).toEqual([
      "Favorites",
      "Favorites/Empty",
      "Favorites/Nested",
      "Favorites/Nested/Leaf",
    ]);
    expect(result.files).toHaveLength(1);
    expect((result.files[0] as File & { webkitRelativePath?: string }).webkitRelativePath).toBe(
      "Favorites/Pokemon Emerald.gba",
    );
  });
});
