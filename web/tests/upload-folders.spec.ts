import { expect, test, type Page } from "@playwright/test";
import JSZip from "jszip";
import { mkdir, writeFile } from "node:fs/promises";
import { dirname } from "node:path";

async function pairAndOpenFiles(page: Page) {
  await page.goto("/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();
  await page.goto("/?view=files");
  await expect(page.getByRole("navigation", { name: "Files path" })).toBeVisible();
}

async function dropDirectoryTree(page: Page, rootName: string) {
  await page.getByTestId("upload-drop-zone").evaluate((zone, name) => {
    const payloadFile = new File(["dropped payload"], "payload.txt", { type: "text/plain" });
    const fileEntry = {
      file: (cb: (file: File) => void) => cb(payloadFile),
      isDirectory: false,
      isFile: true,
      name: "payload.txt",
    };
    const emptyEntry = {
      createReader: () => ({
        readEntries: (cb: (entries: unknown[]) => void) => cb([]),
      }),
      isDirectory: true,
      isFile: false,
      name: "Empty",
    };
    const rootEntry = {
      createReader: () => {
        let index = 0;
        const batches = [[fileEntry, emptyEntry], []];

        return {
          readEntries: (cb: (entries: unknown[]) => void) => {
            cb(batches[index] ?? []);
            index += 1;
          },
        };
      },
      isDirectory: true,
      isFile: false,
      name,
    };
    const event = new Event("drop", { bubbles: true, cancelable: true });

    Object.defineProperty(event, "dataTransfer", {
      value: {
        dropEffect: "copy",
        files: [],
        items: [{ kind: "file", webkitGetAsEntry: () => rootEntry }],
        types: ["Files"],
      },
    });
    zone.dispatchEvent(event);
  }, rootName);
}

async function writeZipFixture(rootName: string, zipPath: string): Promise<string> {
  const zip = new JSZip();

  zip.folder(rootName)?.folder("Empty");
  zip.file(`${rootName}/Data/readme.txt`, "zip payload");
  zip.folder("__MACOSX")?.file("._readme.txt", "ignored");
  zip.file(`${rootName}/.DS_Store`, "ignored");

  await mkdir(dirname(zipPath), { recursive: true });
  await writeFile(zipPath, await zip.generateAsync({ type: "nodebuffer" }));
  return zipPath;
}

test("preserves empty folders through drag/drop and explicit ZIP upload", async ({ page }, testInfo) => {
  const suffix = (testInfo.project.name || "default").replace(/[^a-z0-9]+/gi, "-");
  const droppedRoot = `Dropped-${suffix}`;
  const zipRoot = `ZipRoot-${suffix}`;

  await pairAndOpenFiles(page);
  await dropDirectoryTree(page, droppedRoot);

  await expect(page.getByText("Uploaded 1 file and 2 folders.")).toBeVisible();
  await page.getByRole("button", { name: `Open ${droppedRoot}` }).click();
  await expect(page.getByRole("button", { name: "Open Empty" })).toBeVisible();
  await expect(page.getByRole("link", { name: "Download payload.txt" })).toBeVisible();

  await page.goto("/?view=files");
  const zipPath = await writeZipFixture(zipRoot, testInfo.outputPath(`${zipRoot}.zip`));
  const chooser = page.waitForEvent("filechooser");

  await page.getByRole("button", { name: "Upload ZIP" }).click();
  await (await chooser).setFiles(zipPath);

  await expect(page.getByText("Uploaded 1 file and 3 folders.")).toBeVisible();
  await page.getByRole("button", { name: `Open ${zipRoot}` }).click();
  await expect(page.getByRole("button", { name: "Open Empty" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Data" })).toBeVisible();
});
