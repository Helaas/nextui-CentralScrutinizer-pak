import JSZip from "jszip";

import type { ExtractStrategy, UploadSelection } from "./types";

export type ParsedZipEntry = {
  kind: "directory" | "file";
  path: string;
  zipObject: JSZip.JSZipObject;
};

export type ParsedZipPreview = {
  entries: ParsedZipEntry[];
  commonRoot: string | null;
  totalFiles: number;
  totalDirectories: number;
  archiveFileName: string;
  zipNameWithoutExtension: string;
};

export const ZIP_MAX_ENTRIES = 50000;

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

export function archiveRootFromFileName(name: string): string {
  const withoutExtension = name.replace(/\.(zip|pakz)$/i, "").trim();
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

function findCommonRoot(entries: ParsedZipEntry[]): string | null {
  if (entries.length === 0) {
    return null;
  }

  const root = firstPathSegment(entries[0].path);

  if (!root) {
    return null;
  }

  const allShareRoot = entries.every((entry) => entry.path === root || entry.path.startsWith(`${root}/`));
  const hasTopLevelFile = entries.some((entry) => entry.kind === "file" && !entry.path.includes("/"));
  const hasRootDirectoryEntry = entries.some((entry) => entry.kind === "directory" && entry.path === root);

  if (allShareRoot && (hasRootDirectoryEntry || !hasTopLevelFile)) {
    return root;
  }

  return null;
}

function stripCommonRoot(path: string, commonRoot: string | null): string {
  if (!commonRoot) {
    return path;
  }

  if (path === commonRoot) {
    return "";
  }

  if (path.startsWith(`${commonRoot}/`)) {
    return path.slice(commonRoot.length + 1);
  }

  return path;
}

function applyWrapper(path: string, wrapper: string): string {
  return path ? `${wrapper}/${path}` : wrapper;
}

function addParentDirectories(path: string, directories: Set<string>) {
  const parts = path.split("/");

  for (let i = 1; i < parts.length; i += 1) {
    directories.add(parts.slice(0, i).join("/"));
  }
}

export function computeUploadPath(
  entryPath: string,
  preview: Pick<ParsedZipPreview, "commonRoot" | "zipNameWithoutExtension">,
  strategy: ExtractStrategy,
): string {
  let uploadPath = strategy === "preserve-full-path" ? entryPath : stripCommonRoot(entryPath, preview.commonRoot);

  if (strategy === "extract-into-folder") {
    uploadPath = applyWrapper(uploadPath, preview.zipNameWithoutExtension);
  }

  return uploadPath;
}

export async function parseZipFile(file: File): Promise<ParsedZipPreview> {
  const zip = await JSZip.loadAsync(file);
  const entries: ParsedZipEntry[] = [];
  let totalSeen = 0;

  zip.forEach((relativePath, zipObject) => {
    totalSeen += 1;
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

  if (totalSeen > ZIP_MAX_ENTRIES) {
    throw new Error(
      `ZIP contains too many entries (${totalSeen.toLocaleString()}). Limit is ${ZIP_MAX_ENTRIES.toLocaleString()}.`,
    );
  }

  const commonRoot = findCommonRoot(entries);
  const totalFiles = entries.filter((e) => e.kind === "file").length;
  const totalDirectories = entries.filter((e) => e.kind === "directory").length;

  return {
    entries,
    commonRoot,
    totalFiles,
    totalDirectories,
    archiveFileName: file.name,
    zipNameWithoutExtension: archiveRootFromFileName(file.name),
  };
}

export type ZipUploadPaths = {
  directories: string[];
  explicitDirectories: string[];
  filePaths: string[];
};

export function uploadPathsFromZip(preview: ParsedZipPreview, strategy: ExtractStrategy): ZipUploadPaths {
  const { entries, commonRoot, zipNameWithoutExtension } = preview;
  const directories = new Set<string>();
  const archiveDirectories = new Set<string>();
  const filePaths: string[] = [];

  for (const entry of entries) {
    const uploadPath = computeUploadPath(entry.path, { commonRoot, zipNameWithoutExtension }, strategy);

    if (!uploadPath) {
      continue;
    }

    if (entry.kind === "directory") {
      directories.add(uploadPath);
      archiveDirectories.add(uploadPath);
      continue;
    }

    addParentDirectories(uploadPath, directories);
    filePaths.push(uploadPath);
  }

  const explicitDirectories = Array.from(archiveDirectories).filter(
    (directory) => !filePaths.some((filePath) => filePath.startsWith(`${directory}/`)),
  );

  return {
    directories: Array.from(directories),
    explicitDirectories,
    filePaths,
  };
}

export async function uploadSelectionFromZip(
  preview: ParsedZipPreview,
  strategy: ExtractStrategy,
): Promise<UploadSelection> {
  const { entries, commonRoot, zipNameWithoutExtension } = preview;
  const { directories } = uploadPathsFromZip(preview, strategy);
  const files: File[] = [];

  for (const entry of entries) {
    const uploadPath = computeUploadPath(entry.path, { commonRoot, zipNameWithoutExtension }, strategy);

    if (!uploadPath) {
      continue;
    }

    if (entry.kind === "directory") {
      continue;
    }

    const leaf = uploadPath.split("/").pop() ?? entry.path;
    const blob = await entry.zipObject.async("blob");
    const uploadFile = new File([blob], leaf, {
      lastModified: entry.zipObject.date?.getTime(),
      type: blob.type,
    });

    Object.defineProperty(uploadFile, "webkitRelativePath", {
      configurable: true,
      enumerable: true,
      value: uploadPath,
      writable: false,
    });
    files.push(uploadFile);
  }

  return {
    directories,
    files,
  };
}
