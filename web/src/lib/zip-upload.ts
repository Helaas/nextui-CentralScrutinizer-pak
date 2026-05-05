import JSZip from "jszip";

import type { UploadSelection } from "./types";

type ArchiveEntry = {
  kind: "directory" | "file";
  path: string;
  zipObject: JSZip.JSZipObject;
};

function normalizeArchivePath(path: string, isDirectory: boolean): string {
  let normalized = path.replace(/\\/g, "/");

  while (normalized.startsWith("./")) {
    normalized = normalized.slice(2);
  }
  if (isDirectory) {
    normalized = normalized.replace(/\/+$/g, "");
  }

  return normalized;
}

function archiveRootFromFileName(name: string): string {
  const withoutExtension = name.replace(/\.zip$/i, "").trim();
  const normalized = withoutExtension
    .replace(/[\\/]+/g, "-")
    .replace(/[\x00-\x1f]/g, "-")
    .replace(/[ .]+$/g, "")
    .trim();

  return normalized || "Archive";
}

function isMacArchiveArtifact(path: string): boolean {
  const segments = path.split("/").filter(Boolean);
  const leaf = segments[segments.length - 1] ?? "";

  return segments.includes("__MACOSX") || leaf === ".DS_Store" || leaf.startsWith("._");
}

function firstPathSegment(path: string): string {
  const slash = path.indexOf("/");

  return slash >= 0 ? path.slice(0, slash) : path;
}

function shouldPreserveArchiveRoot(entries: ArchiveEntry[]): boolean {
  if (entries.length === 0) {
    return true;
  }

  const root = firstPathSegment(entries[0].path);

  if (!root) {
    return true;
  }

  const allShareRoot = entries.every((entry) => entry.path === root || entry.path.startsWith(`${root}/`));
  const hasTopLevelFile = entries.some((entry) => entry.kind === "file" && !entry.path.includes("/"));
  const hasRootDirectoryEntry = entries.some((entry) => entry.kind === "directory" && entry.path === root);

  return allShareRoot && (hasRootDirectoryEntry || !hasTopLevelFile);
}

function withArchiveWrapper(path: string, wrapper: string): string {
  return path ? `${wrapper}/${path}` : wrapper;
}

function addParentDirectories(path: string, directories: Set<string>) {
  const parts = path.split("/");

  for (let i = 1; i < parts.length; i += 1) {
    directories.add(parts.slice(0, i).join("/"));
  }
}

export async function uploadSelectionFromZip(file: File): Promise<UploadSelection> {
  const zip = await JSZip.loadAsync(file);
  const entries: ArchiveEntry[] = [];

  zip.forEach((relativePath, zipObject) => {
    const normalized = normalizeArchivePath(relativePath, zipObject.dir);

    if (!normalized || isMacArchiveArtifact(normalized)) {
      return;
    }

    entries.push({
      kind: zipObject.dir ? "directory" : "file",
      path: normalized,
      zipObject,
    });
  });

  const preserveRoot = shouldPreserveArchiveRoot(entries);
  const wrapper = archiveRootFromFileName(file.name);
  const directories = new Set<string>();
  const files: File[] = [];

  for (const entry of entries) {
    const uploadPath = preserveRoot ? entry.path : withArchiveWrapper(entry.path, wrapper);

    if (entry.kind === "directory") {
      directories.add(uploadPath);
      continue;
    }

    addParentDirectories(uploadPath, directories);
    const leaf = uploadPath.split("/").pop() ?? entry.path;
    const blob = await entry.zipObject.async("blob");
    const uploadFile = new File([blob], leaf, {
      lastModified: entry.zipObject.date?.getTime(),
      type: blob.type,
    });

    Object.defineProperty(uploadFile, "webkitRelativePath", {
      configurable: true,
      value: uploadPath,
    });
    files.push(uploadFile);
  }

  return {
    directories: Array.from(directories),
    files,
  };
}
