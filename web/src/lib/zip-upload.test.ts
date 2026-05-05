import JSZip from "jszip";
import { describe, expect, it } from "vitest";

import { parseZipFile, uploadSelectionFromZip } from "./zip-upload";

async function makeZipFile(name: string, build: (zip: JSZip) => void): Promise<File> {
  const zip = new JSZip();

  build(zip);

  const blob = await zip.generateAsync({ type: "blob" });

  return new File([blob], name, { type: "application/zip" });
}

function relativePaths(files: File[]): string[] {
  return files.map((file) => (file as File & { webkitRelativePath?: string }).webkitRelativePath ?? file.name);
}

describe("parseZipFile", () => {
  it("returns parsed entries, common root, and counts", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const preview = await parseZipFile(file);

    expect(preview.commonRoot).toBe("Favorites");
    expect(preview.totalFiles).toBe(1);
    expect(preview.totalDirectories).toBe(3);
    expect(preview.zipNameWithoutExtension).toBe("favorites");
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
  });

  it("returns empty for zip with no uploadable content", async () => {
    const file = await makeZipFile("empty.zip", (zip) => {
      zip.folder("__MACOSX")?.file("._readme.txt", "sidecar");
    });

    const preview = await parseZipFile(file);

    expect(preview.entries).toHaveLength(0);
    expect(preview.commonRoot).toBeNull();
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
});
