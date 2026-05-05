import JSZip from "jszip";
import { afterEach, describe, expect, it, vi } from "vitest";

import {
  ZIP_MAX_ENTRIES,
  ZIP_MAX_UNCOMPRESSED_BYTES,
  archiveRootFromFileName,
  parseZipFile,
  uploadPathsFromZip,
  uploadSelectionFromZip,
} from "./zip-upload";

async function makeZipFile(name: string, build: (zip: JSZip) => void): Promise<File> {
  const zip = new JSZip();

  build(zip);

  const blob = await zip.generateAsync({ type: "blob" });

  return new File([blob], name, { type: "application/zip" });
}

function relativePaths(files: File[]): string[] {
  return files.map((file) => (file as File & { webkitRelativePath?: string }).webkitRelativePath ?? file.name);
}

function makeZipMetadataBytes(
  entries: Array<{
    isDirectory?: boolean;
    name: string;
    uncompressedSize?: number;
  }>,
): Uint8Array {
  const encoder = new TextEncoder();
  const centralDirectoryEntries = entries.map((entry) => {
    const rawName = entry.isDirectory && !entry.name.endsWith("/") ? `${entry.name}/` : entry.name;
    const fileName = encoder.encode(rawName);
    const header = new Uint8Array(46 + fileName.length);
    const view = new DataView(header.buffer);

    view.setUint32(0, 0x02014b50, true);
    view.setUint16(4, 20, true);
    view.setUint16(6, 20, true);
    view.setUint16(8, 0x0800, true);
    view.setUint32(20, 0, true);
    view.setUint32(24, entry.uncompressedSize ?? 0, true);
    view.setUint16(28, fileName.length, true);
    view.setUint32(38, entry.isDirectory ? 0x10 : 0, true);
    header.set(fileName, 46);
    return header;
  });
  const centralDirectorySize = centralDirectoryEntries.reduce((total, entry) => total + entry.length, 0);
  const eocd = new Uint8Array(22);
  const eocdView = new DataView(eocd.buffer);

  eocdView.setUint32(0, 0x06054b50, true);
  eocdView.setUint16(8, entries.length, true);
  eocdView.setUint16(10, entries.length, true);
  eocdView.setUint32(12, centralDirectorySize, true);
  eocdView.setUint32(16, 0, true);

  const archive = new Uint8Array(centralDirectorySize + eocd.length);
  let cursor = 0;

  for (const entry of centralDirectoryEntries) {
    archive.set(entry, cursor);
    cursor += entry.length;
  }
  archive.set(eocd, cursor);
  return archive;
}

describe("parseZipFile", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("returns parsed entries, common root, and counts", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const preview = await parseZipFile(file);

    expect(preview.commonRoot).toBe("Favorites");
    expect(preview.totalFiles).toBe(1);
    expect(preview.totalDirectories).toBe(3);
    expect(preview.archiveFileName).toBe("favorites.zip");
    expect(preview.zipNameWithoutExtension).toBe("favorites");
    expect(preview.totalUncompressedBytes).toBe(3);
    expect(preview.entries).toHaveLength(4);
  });

  it("returns null common root for loose files", async () => {
    const file = await makeZipFile("Loose Files.zip", (zip) => {
      zip.folder("Empty");
      zip.file("readme.txt", "notes");
    });

    const preview = await parseZipFile(file);

    expect(preview.commonRoot).toBeNull();
    expect(preview.totalFiles).toBe(1);
    expect(preview.totalDirectories).toBe(1);
    expect(preview.totalUncompressedBytes).toBe(5);
    expect(preview.zipNameWithoutExtension).toBe("Loose Files");
  });

  it("filters macOS artifacts", async () => {
    const file = await makeZipFile("clean.zip", (zip) => {
      zip.folder("__MACOSX")?.file("._readme.txt", "sidecar");
      zip.file("Root/.DS_Store", "store");
      zip.file("Root/._game.gba", "sidecar");
      zip.folder("Root")?.folder("Empty");
      zip.file("Root/game.gba", "rom");
    });

    const preview = await parseZipFile(file);

    expect(preview.entries.map((e) => e.path)).toEqual(["Root", "Root/Empty", "Root/game.gba"]);
    expect(preview.commonRoot).toBe("Root");
    expect(preview.totalUncompressedBytes).toBe(3);
  });

  it("returns empty for zip with no uploadable content", async () => {
    const file = await makeZipFile("empty.zip", (zip) => {
      zip.folder("__MACOSX")?.file("._readme.txt", "sidecar");
    });

    const preview = await parseZipFile(file);

    expect(preview.entries).toHaveLength(0);
    expect(preview.commonRoot).toBeNull();
    expect(preview.totalUncompressedBytes).toBe(0);
  });

  it("strips .pakz when building the wrapper folder name", async () => {
    const file = await makeZipFile("Central.Scrutinizer.pakz", (zip) => {
      zip.file("Tools/tg5040/Central Scrutinizer.pak/pak.json", "{}");
    });

    const preview = await parseZipFile(file);

    expect(preview.archiveFileName).toBe("Central.Scrutinizer.pakz");
    expect(preview.zipNameWithoutExtension).toBe("Central.Scrutinizer");
  });

  it("rejects archives whose uploadable entries exceed the uncompressed size limit before loading with JSZip", async () => {
    const loadAsyncSpy = vi.spyOn(JSZip, "loadAsync");
    const file = new File(
      [makeZipMetadataBytes([{ name: "huge.bin", uncompressedSize: ZIP_MAX_UNCOMPRESSED_BYTES + 1 }])],
      "huge.zip",
      { type: "application/zip" },
    );

    await expect(parseZipFile(file)).rejects.toThrow(
      "ZIP expands to too much data",
    );
    expect(loadAsyncSpy).not.toHaveBeenCalled();
  });

  it("counts only uploadable entries toward the ZIP entry limit", async () => {
    const zip = {
      forEach: (callback: (relativePath: string, zipObject: JSZip.JSZipObject) => void) => {
        callback("game.gba", {
          async: vi.fn(),
          comment: "",
          date: new Date(),
          dir: false,
          dosPermissions: null,
          name: "game.gba",
          options: {},
          unixPermissions: null,
        } as unknown as JSZip.JSZipObject);
      },
    } as unknown as JSZip;
    const file = new File(
      [
        makeZipMetadataBytes([
          ...Array.from({ length: ZIP_MAX_ENTRIES }, (_, index) => ({
            name: `__MACOSX/._artifact-${index}`,
            uncompressedSize: 1,
          })),
          { name: "game.gba", uncompressedSize: 3 },
        ]),
      ],
      "clean.zip",
      { type: "application/zip" },
    );

    vi.spyOn(JSZip, "loadAsync").mockResolvedValue(zip);

    const preview = await parseZipFile(file);

    expect(preview.entries.map((entry) => entry.path)).toEqual(["game.gba"]);
    expect(preview.totalUncompressedBytes).toBe(3);
  });
});

describe("archiveRootFromFileName", () => {
  it("sanitizes invalid path characters and reserved device names", () => {
    expect(archiveRootFromFileName("bad:name?.zip")).toBe("bad-name");
    expect(archiveRootFromFileName("CON.zip")).toBe("archive-CON");
    expect(archiveRootFromFileName("con.txt.zip")).toBe("archive-con.txt");
  });
});

describe("uploadSelectionFromZip", () => {
  it("extract-here strips common root", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "extract-here");

    expect(selection.directories).toEqual(["Empty", "GBA"]);
    expect(relativePaths(selection.files)).toEqual(["GBA/Pokemon Emerald.gba"]);
  });

  it("extract-here keeps loose files as-is", async () => {
    const file = await makeZipFile("Loose Files.zip", (zip) => {
      zip.folder("Empty");
      zip.file("readme.txt", "notes");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "extract-here");

    expect(selection.directories).toEqual(["Empty"]);
    expect(relativePaths(selection.files)).toEqual(["readme.txt"]);
  });

  it("extract-into-folder wraps under zip name and strips root", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "extract-into-folder");

    expect(selection.directories).toEqual(["favorites", "favorites/Empty", "favorites/GBA"]);
    expect(relativePaths(selection.files)).toEqual(["favorites/GBA/Pokemon Emerald.gba"]);
  });

  it("extract-into-folder wraps loose files under zip name", async () => {
    const file = await makeZipFile("Loose Files.zip", (zip) => {
      zip.folder("Empty");
      zip.file("readme.txt", "notes");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "extract-into-folder");

    expect(selection.directories).toEqual(["Loose Files/Empty", "Loose Files"]);
    expect(relativePaths(selection.files)).toEqual(["Loose Files/readme.txt"]);
  });

  it("preserve-full-path keeps archive roots exactly as stored", async () => {
    const file = await makeZipFile("Central.Scrutinizer.pakz", (zip) => {
      zip.folder("Tools")?.folder("tg5040")?.folder("Central Scrutinizer.pak");
      zip.file("Tools/tg5040/Central Scrutinizer.pak/pak.json", "{}");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "preserve-full-path");

    expect(selection.directories).toContain("Tools");
    expect(selection.directories).toContain("Tools/tg5040");
    expect(selection.directories).toContain("Tools/tg5040/Central Scrutinizer.pak");
    expect(relativePaths(selection.files)).toEqual(["Tools/tg5040/Central Scrutinizer.pak/pak.json"]);
  });

  it("skips macOS artifacts for both strategies", async () => {
    const file = await makeZipFile("clean.zip", (zip) => {
      zip.folder("__MACOSX")?.file("._readme.txt", "sidecar");
      zip.file("Root/.DS_Store", "store");
      zip.file("Root/._game.gba", "sidecar");
      zip.folder("Root")?.folder("Empty");
      zip.file("Root/game.gba", "rom");
    });

    const preview = await parseZipFile(file);
    const selectionHere = await uploadSelectionFromZip(preview, "extract-here");
    const selectionFolder = await uploadSelectionFromZip(preview, "extract-into-folder");

    expect(selectionHere.directories).toEqual(["Empty"]);
    expect(relativePaths(selectionHere.files)).toEqual(["game.gba"]);

    expect(selectionFolder.directories).toEqual(["clean", "clean/Empty"]);
    expect(relativePaths(selectionFolder.files)).toEqual(["clean/game.gba"]);
  });

  it("passes unsafe paths through so the upload route can reject them", async () => {
    const file = await makeZipFile("unsafe.zip", (zip) => {
      zip.folder("Root")?.folder("trailing-space ");
    });

    const preview = await parseZipFile(file);
    const selection = await uploadSelectionFromZip(preview, "extract-here");

    expect(selection.directories).toContain("trailing-space ");
  });

  it("only previews explicit empty directories for conflict preflight", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const preview = await parseZipFile(file);
    const uploadPaths = uploadPathsFromZip(preview, "extract-into-folder");

    expect(uploadPaths.directories).toEqual(["favorites", "favorites/Empty", "favorites/GBA"]);
    expect(uploadPaths.explicitDirectories).toEqual(["favorites/Empty"]);
    expect(uploadPaths.filePaths).toEqual(["favorites/GBA/Pokemon Emerald.gba"]);
  });
});
