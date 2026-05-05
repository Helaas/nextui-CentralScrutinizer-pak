import JSZip from "jszip";
import { describe, expect, it } from "vitest";

import { uploadSelectionFromZip } from "./zip-upload";

async function makeZipFile(name: string, build: (zip: JSZip) => void): Promise<File> {
  const zip = new JSZip();

  build(zip);

  const blob = await zip.generateAsync({ type: "blob" });

  return new File([blob], name, { type: "application/zip" });
}

function relativePaths(files: File[]): string[] {
  return files.map((file) => (file as File & { webkitRelativePath?: string }).webkitRelativePath ?? file.name);
}

describe("uploadSelectionFromZip", () => {
  it("preserves a single archive root and empty directories", async () => {
    const file = await makeZipFile("favorites.zip", (zip) => {
      zip.folder("Favorites")?.folder("Empty");
      zip.file("Favorites/GBA/Pokemon Emerald.gba", "rom");
    });

    const selection = await uploadSelectionFromZip(file);

    expect(selection.directories).toEqual(["Favorites", "Favorites/Empty", "Favorites/GBA"]);
    expect(relativePaths(selection.files)).toEqual(["Favorites/GBA/Pokemon Emerald.gba"]);
  });

  it("wraps loose archive entries under the zip basename", async () => {
    const file = await makeZipFile("Loose Files.zip", (zip) => {
      zip.folder("Empty");
      zip.file("readme.txt", "notes");
    });

    const selection = await uploadSelectionFromZip(file);

    expect(selection.directories).toEqual(["Loose Files/Empty", "Loose Files"]);
    expect(relativePaths(selection.files)).toEqual(["Loose Files/readme.txt"]);
  });

  it("skips common macOS archive artifacts", async () => {
    const file = await makeZipFile("clean.zip", (zip) => {
      zip.folder("__MACOSX")?.file("._readme.txt", "sidecar");
      zip.file("Root/.DS_Store", "store");
      zip.file("Root/._game.gba", "sidecar");
      zip.folder("Root")?.folder("Empty");
      zip.file("Root/game.gba", "rom");
    });

    const selection = await uploadSelectionFromZip(file);

    expect(selection.directories).toEqual(["Root", "Root/Empty"]);
    expect(relativePaths(selection.files)).toEqual(["Root/game.gba"]);
  });

  it("passes unsafe paths through so the upload route can reject them", async () => {
    const file = await makeZipFile("unsafe.zip", (zip) => {
      zip.folder("Root")?.folder("trailing-space ");
    });

    const selection = await uploadSelectionFromZip(file);

    expect(selection.directories).toContain("Root/trailing-space ");
  });
});
