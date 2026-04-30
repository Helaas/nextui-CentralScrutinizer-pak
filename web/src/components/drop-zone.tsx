import { useCallback, useRef, useState, type ReactNode } from "react";

const EMPTY_IGNORED_DRAG_TYPES: readonly string[] = [];

type FileSystemEntryLike = {
  isFile: boolean;
  isDirectory: boolean;
  name: string;
  file?: (cb: (file: File) => void, err?: (e: unknown) => void) => void;
  createReader?: () => { readEntries: (cb: (entries: FileSystemEntryLike[]) => void) => void };
};

async function readFileEntry(entry: FileSystemEntryLike, path: string): Promise<File> {
  return new Promise<File>((resolve, reject) => {
    entry.file!((file) => {
      if (path) {
        Object.defineProperty(file, "webkitRelativePath", { value: `${path}${file.name}`, writable: false });
      }
      resolve(file);
    }, reject);
  });
}

async function readDirectoryEntries(reader: ReturnType<NonNullable<FileSystemEntryLike["createReader"]>>): Promise<FileSystemEntryLike[]> {
  const entries: FileSystemEntryLike[] = [];

  while (true) {
    const batch = await new Promise<FileSystemEntryLike[]>((resolve) => {
      reader.readEntries(resolve);
    });

    if (batch.length === 0) {
      return entries;
    }

    entries.push(...batch);
  }
}

async function collectFilesFromEntry(entry: FileSystemEntryLike, path: string): Promise<File[]> {
  if (entry.isFile && entry.file) {
    const file = await readFileEntry(entry, path);
    return [file];
  }

  if (entry.isDirectory && entry.createReader) {
    const reader = entry.createReader();
    const children = await readDirectoryEntries(reader);
    const nested = await Promise.all(
      children.map((child) => collectFilesFromEntry(child, `${path}${entry.name}/`)),
    );
    return nested.flat();
  }

  return [];
}

export async function collectDroppedFiles(
  dataTransfer: DataTransfer,
  { allowDirectories = true }: { allowDirectories?: boolean } = {},
): Promise<File[]> {
  const entries: FileSystemEntryLike[] = [];

  for (let i = 0; i < dataTransfer.items.length; i++) {
    const item = dataTransfer.items[i];

    if (item.kind !== "file") {
      continue;
    }

    const entry = (item as DataTransferItem & { webkitGetAsEntry?: () => FileSystemEntryLike | null }).webkitGetAsEntry?.();

    if (entry && (allowDirectories || !entry.isDirectory)) {
      entries.push(entry);
    }
  }

  if (entries.length > 0) {
    const nested = await Promise.all(entries.map((entry) => collectFilesFromEntry(entry, "")));
    return nested.flat();
  }

  return Array.from(dataTransfer.files);
}

function hasIgnoredDragType(dataTransfer: DataTransfer, ignoredDragTypes: readonly string[]): boolean {
  if (ignoredDragTypes.length === 0) {
    return false;
  }

  const types = Array.from(dataTransfer.types ?? []);

  return ignoredDragTypes.some((type) => types.includes(type));
}

export function DropZone({
  allowDirectories = true,
  children,
  disabled = false,
  ignoredDragTypes = EMPTY_IGNORED_DRAG_TYPES,
  onDrop,
}: {
  allowDirectories?: boolean;
  children: ReactNode;
  disabled?: boolean;
  ignoredDragTypes?: readonly string[];
  onDrop: (files: File[]) => void;
}) {
  const [dragActive, setDragActive] = useState(false);
  const [reading, setReading] = useState(false);
  const dragCounter = useRef(0);

  const handleDragEnter = useCallback(
    (event: React.DragEvent) => {
      if (hasIgnoredDragType(event.dataTransfer, ignoredDragTypes)) {
        dragCounter.current = 0;
        setDragActive(false);
        return;
      }

      event.preventDefault();
      if (disabled) return;
      dragCounter.current += 1;
      if (dragCounter.current === 1) {
        setDragActive(true);
      }
    },
    [disabled, ignoredDragTypes],
  );

  const handleDragOver = useCallback(
    (event: React.DragEvent) => {
      if (hasIgnoredDragType(event.dataTransfer, ignoredDragTypes)) {
        return;
      }

      event.preventDefault();
      if (!disabled) {
        event.dataTransfer.dropEffect = "copy";
      }
    },
    [disabled, ignoredDragTypes],
  );

  const handleDragLeave = useCallback(
    (event: React.DragEvent) => {
      event.preventDefault();
      dragCounter.current -= 1;
      if (dragCounter.current <= 0) {
        dragCounter.current = 0;
        setDragActive(false);
      }
    },
    [],
  );

  const handleDrop = useCallback(
    async (event: React.DragEvent) => {
      event.preventDefault();
      dragCounter.current = 0;
      setDragActive(false);

      if (hasIgnoredDragType(event.dataTransfer, ignoredDragTypes)) {
        return;
      }

      if (disabled) return;

      setReading(true);
      try {
        const files = await collectDroppedFiles(event.dataTransfer, { allowDirectories });
        if (files.length > 0) {
          onDrop(files);
        }
      } finally {
        setReading(false);
      }
    },
    [allowDirectories, disabled, ignoredDragTypes, onDrop],
  );

  return (
    <div
      className="relative"
      onDragEnter={handleDragEnter}
      onDragLeave={handleDragLeave}
      onDragOver={handleDragOver}
      onDrop={handleDrop}
    >
      {children}
      {(dragActive || reading) && !disabled ? (
        <div
          aria-label={reading ? "Reading dropped files" : "Drop files here to upload"}
          className="pointer-events-none absolute inset-0 z-40 flex items-center justify-center rounded-[24px] border-2 border-dashed border-[var(--accent)] bg-[var(--accent-soft)]"
        >
          <div className="flex flex-col items-center gap-2 text-center">
            <span aria-hidden="true" className="text-3xl">
              ⬆
            </span>
            <p className="text-sm font-semibold text-[var(--text)]">
              {reading ? "Reading files…" : "Drop files here to upload"}
            </p>
          </div>
        </div>
      ) : null}
    </div>
  );
}
