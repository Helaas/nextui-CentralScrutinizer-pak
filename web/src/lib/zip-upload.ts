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
  totalUncompressedBytes: number;
  archiveFileName: string;
  zipNameWithoutExtension: string;
};

export const ZIP_MAX_ENTRIES = 50000;
export const ZIP_MAX_UNCOMPRESSED_BYTES = 2 * 1024 * 1024 * 1024;

type ZipMetadataEntry = {
  isDirectory: boolean;
  path: string;
  uncompressedSize: number;
};

const ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIGNATURE = 0x02014b50;
const ZIP_END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06054b50;
const ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE = 0x07064b50;
const ZIP64_END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06064b50;
const ZIP64_EXTRA_FIELD_HEADER_ID = 0x0001;
const ZIP_DIRECTORY_MARKER = "/";
const RESERVED_WINDOWS_NAME_RE = /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i;
const INVALID_PATH_COMPONENT_CHARS_RE = /[\x00-\x1f\\/:*?"<>|]+/g;
const ZIP_NAME_DECODER = new TextDecoder("utf-8");

function formatBytes(bytes: number): string {
  const gib = 1024 * 1024 * 1024;
  const mib = 1024 * 1024;

  if (bytes >= gib) {
    const value = bytes / gib;
    return `${Number.isInteger(value) ? value.toFixed(0) : value.toFixed(1)} GiB`;
  }
  if (bytes >= mib) {
    const value = bytes / mib;
    return `${Number.isInteger(value) ? value.toFixed(0) : value.toFixed(1)} MiB`;
  }

  return `${bytes.toLocaleString()} bytes`;
}

function readUint64(view: DataView, offset: number): bigint {
  const low = BigInt(view.getUint32(offset, true));
  const high = BigInt(view.getUint32(offset + 4, true));

  return low | (high << 32n);
}

function toSafeNumber(value: bigint, label: string): number {
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new Error(`ZIP ${label} is too large to inspect safely.`);
  }

  return Number(value);
}

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
  let normalized = withoutExtension
    .replace(/[\\/]+/g, "-")
    .replace(INVALID_PATH_COMPONENT_CHARS_RE, "-")
    .replace(/-+/g, "-")
    .trim()
    .replace(/^-+/g, "")
    .replace(/[ .-]+$/g, "");

  if (normalized === "." || normalized === "..") {
    normalized = "";
  }
  if (RESERVED_WINDOWS_NAME_RE.test(normalized)) {
    normalized = `archive-${normalized}`;
  }

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

async function readFileArrayBuffer(file: File): Promise<ArrayBuffer> {
  const arrayBuffer = (file as File & { arrayBuffer?: () => Promise<ArrayBuffer> }).arrayBuffer;

  if (typeof arrayBuffer === "function") {
    return arrayBuffer.call(file);
  }

  return new Promise<ArrayBuffer>((resolve, reject) => {
    const reader = new FileReader();

    reader.onerror = () => {
      reject(reader.error ?? new Error("ZIP file could not be read."));
    };
    reader.onload = () => {
      if (reader.result instanceof ArrayBuffer) {
        resolve(reader.result);
        return;
      }
      reject(new Error("ZIP file could not be read."));
    };
    reader.readAsArrayBuffer(file);
  });
}

function findEndOfCentralDirectory(view: DataView): number {
  const minimumLength = 22;

  if (view.byteLength < minimumLength) {
    throw new Error("ZIP central directory could not be read.");
  }

  const searchStart = Math.max(0, view.byteLength - (0xffff + minimumLength));

  for (let offset = view.byteLength - minimumLength; offset >= searchStart; offset -= 1) {
    if (view.getUint32(offset, true) !== ZIP_END_OF_CENTRAL_DIRECTORY_SIGNATURE) {
      continue;
    }

    const commentLength = view.getUint16(offset + 20, true);

    if (offset + minimumLength + commentLength === view.byteLength) {
      return offset;
    }
  }

  throw new Error("ZIP central directory could not be read.");
}

function readZip64CentralDirectory(view: DataView, eocdOffset: number): {
  centralDirectoryOffset: number;
  centralDirectorySize: number;
  totalEntries: number;
} {
  const locatorOffset = eocdOffset - 20;

  if (locatorOffset < 0 || view.getUint32(locatorOffset, true) !== ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR_SIGNATURE) {
    throw new Error("ZIP central directory could not be read.");
  }

  const zip64EocdOffset = toSafeNumber(readUint64(view, locatorOffset + 8), "metadata");

  if (zip64EocdOffset < 0 || zip64EocdOffset + 56 > view.byteLength) {
    throw new Error("ZIP central directory could not be read.");
  }
  if (view.getUint32(zip64EocdOffset, true) !== ZIP64_END_OF_CENTRAL_DIRECTORY_SIGNATURE) {
    throw new Error("ZIP central directory could not be read.");
  }

  return {
    totalEntries: toSafeNumber(readUint64(view, zip64EocdOffset + 32), "entry count"),
    centralDirectorySize: toSafeNumber(readUint64(view, zip64EocdOffset + 40), "metadata"),
    centralDirectoryOffset: toSafeNumber(readUint64(view, zip64EocdOffset + 48), "metadata"),
  };
}

function readZip64EntryUncompressedSize(
  view: DataView,
  entryOffset: number,
  extraFieldOffset: number,
  extraFieldLength: number,
): number {
  const extraFieldEnd = extraFieldOffset + extraFieldLength;
  let cursor = extraFieldOffset;

  while (cursor + 4 <= extraFieldEnd) {
    const headerId = view.getUint16(cursor, true);
    const dataLength = view.getUint16(cursor + 2, true);
    const dataOffset = cursor + 4;
    const dataEnd = dataOffset + dataLength;

    if (dataEnd > extraFieldEnd) {
      break;
    }
    if (headerId === ZIP64_EXTRA_FIELD_HEADER_ID) {
      if (view.getUint32(entryOffset + 24, true) === 0xffffffff) {
        if (dataOffset + 8 > dataEnd) {
          break;
        }
        return toSafeNumber(readUint64(view, dataOffset), "entry size");
      }
      break;
    }

    cursor = dataEnd;
  }

  throw new Error("ZIP entry size could not be read.");
}

function inspectZipArchive(buffer: ArrayBuffer): ZipMetadataEntry[] {
  const view = new DataView(buffer);
  const eocdOffset = findEndOfCentralDirectory(view);

  let totalEntries = view.getUint16(eocdOffset + 10, true);
  let centralDirectorySize = view.getUint32(eocdOffset + 12, true);
  let centralDirectoryOffset = view.getUint32(eocdOffset + 16, true);

  if (totalEntries === 0xffff || centralDirectorySize === 0xffffffff || centralDirectoryOffset === 0xffffffff) {
    const zip64 = readZip64CentralDirectory(view, eocdOffset);

    totalEntries = zip64.totalEntries;
    centralDirectorySize = zip64.centralDirectorySize;
    centralDirectoryOffset = zip64.centralDirectoryOffset;
  }

  if (centralDirectoryOffset + centralDirectorySize > view.byteLength) {
    throw new Error("ZIP central directory could not be read.");
  }

  const entries: ZipMetadataEntry[] = [];
  let cursor = centralDirectoryOffset;

  for (let entryIndex = 0; entryIndex < totalEntries; entryIndex += 1) {
    if (cursor + 46 > centralDirectoryOffset + centralDirectorySize) {
      throw new Error("ZIP central directory could not be read.");
    }
    if (view.getUint32(cursor, true) !== ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIGNATURE) {
      throw new Error("ZIP central directory could not be read.");
    }

    const fileNameLength = view.getUint16(cursor + 28, true);
    const extraFieldLength = view.getUint16(cursor + 30, true);
    const commentLength = view.getUint16(cursor + 32, true);
    const fileNameOffset = cursor + 46;
    const extraFieldOffset = fileNameOffset + fileNameLength;
    const nextEntryOffset = extraFieldOffset + extraFieldLength + commentLength;

    if (nextEntryOffset > centralDirectoryOffset + centralDirectorySize) {
      throw new Error("ZIP central directory could not be read.");
    }

    const rawName = ZIP_NAME_DECODER.decode(new Uint8Array(buffer, fileNameOffset, fileNameLength));
    const isDirectory = rawName.endsWith(ZIP_DIRECTORY_MARKER);
    const normalized = normalizeArchivePath(rawName, isDirectory);

    if (!normalized || isMacArchiveArtifact(normalized)) {
      cursor = nextEntryOffset;
      continue;
    }

    const uncompressedSize = isDirectory
      ? 0
      : view.getUint32(cursor + 24, true) === 0xffffffff
        ? readZip64EntryUncompressedSize(view, cursor, extraFieldOffset, extraFieldLength)
        : view.getUint32(cursor + 24, true);

    entries.push({
      isDirectory,
      path: normalized,
      uncompressedSize,
    });
    cursor = nextEntryOffset;
  }

  return entries;
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
  const metadataEntries = inspectZipArchive(await readFileArrayBuffer(file));
  const totalUncompressedBytes = metadataEntries.reduce((total, entry) => total + entry.uncompressedSize, 0);

  if (metadataEntries.length > ZIP_MAX_ENTRIES) {
    throw new Error(
      `ZIP contains too many entries (${metadataEntries.length.toLocaleString()}). Limit is ${ZIP_MAX_ENTRIES.toLocaleString()}.`,
    );
  }
  if (totalUncompressedBytes > ZIP_MAX_UNCOMPRESSED_BYTES) {
    throw new Error(
      `ZIP expands to too much data (${formatBytes(totalUncompressedBytes)}). Limit is ${formatBytes(ZIP_MAX_UNCOMPRESSED_BYTES)}.`,
    );
  }

  const zip = await JSZip.loadAsync(file);
  const entries: ParsedZipEntry[] = [];

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

  const commonRoot = findCommonRoot(entries);
  const totalFiles = entries.filter((e) => e.kind === "file").length;
  const totalDirectories = entries.filter((e) => e.kind === "directory").length;

  return {
    entries,
    commonRoot,
    totalFiles,
    totalDirectories,
    totalUncompressedBytes,
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
