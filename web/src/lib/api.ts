import type {
  BrowserResponse,
  BrowserScope,
  FileSearchResponse,
  LogsResponse,
  MacDotfilesResponse,
  MutationRequest,
  PlatformsResponse,
  ReplaceArtRequest,
  RenameRequest,
  SaveStatesResponse,
  SessionResponse,
  StatusResponse,
  TerminalSessionResponse,
  UploadPreviewRequest,
  UploadPreviewResponse,
  UploadRequest,
  WriteRequest,
} from "./types";

export class ApiError extends Error {
  code?: string;
  path?: string;
  status: number;

  constructor(message: string, status: number, code?: string, path?: string) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
    this.path = path;
  }
}

export class UploadAbortedError extends Error {
  constructor() {
    super("Upload cancelled");
    this.name = "UploadAbortedError";
  }
}

export type UploadHandle = {
  cancel: () => void;
  promise: Promise<void>;
};

export type UploadSummary = {
  uploaded: number;
  failed: number;
  directoriesCreated: number;
  directoriesFailed: number;
  cancelled: boolean;
  errorMessage?: string | null;
};

export type UploadBatchedHandle = {
  cancel: () => void;
  promise: Promise<UploadSummary>;
};

const UPLOAD_PREVIEW_DISPLAY_LIMIT = 5;

export const PAIRING_UNAVAILABLE_MESSAGE =
  "Pairing is unavailable while the app is running in background mode. Reopen it on the handheld to pair or change settings.";

function toQuery(params: Record<string, string | undefined>): string {
  const search = new URLSearchParams();

  for (const [key, value] of Object.entries(params)) {
    if (value) {
      search.set(key, value);
    }
  }

  const serialized = search.toString();
  return serialized ? `?${serialized}` : "";
}

function csrfHeaders(csrf?: string | null): HeadersInit | undefined {
  return csrf ? { "X-CS-CSRF": csrf } : undefined;
}

async function expectJson<T>(response: Response, errorMessage: string): Promise<T> {
  if (!response.ok) {
    let errorCode: string | undefined;

    try {
      const body = (await response.json()) as { error?: string };

      if (typeof body?.error === "string") {
        errorCode = body.error;
      }
    } catch {
      // Ignore non-JSON error responses.
    }

    if (errorCode === "pairing_throttled") {
      throw new ApiError("Pairing is temporarily locked. Try again shortly.", response.status, errorCode);
    }
    if (errorCode === "invalid_code") {
      throw new ApiError("PIN is invalid or already used. Refresh it on the device and try again.", response.status, errorCode);
    }
    if (errorCode === "qr_expired") {
      throw new ApiError("QR code expired. Refresh it on the device and try again.", response.status, errorCode);
    }
    if (errorCode === "invalid_qr_token") {
      throw new ApiError("QR code is no longer valid. Refresh it on the device and try again.", response.status, errorCode);
    }
    if (errorCode === "pairing_unavailable") {
      throw new ApiError(PAIRING_UNAVAILABLE_MESSAGE, response.status, errorCode);
    }

    throw new ApiError(errorMessage, response.status, errorCode);
  }

  return response.json() as Promise<T>;
}

async function readErrorCode(response: Response): Promise<string | undefined> {
  try {
    const body = (await response.json()) as { error?: string };

    return typeof body?.error === "string" ? body.error : undefined;
  } catch {
    return undefined;
  }
}

function parseUploadError(xhr: XMLHttpRequest): Error {
  try {
    const body = JSON.parse(xhr.responseText) as { error?: string; path?: string };
    const errorPath = typeof body.path === "string" && body.path.length > 0 ? body.path : undefined;
    const pathLabel = errorPath ? ` "${errorPath}"` : "";

    if (body.error === "upload_conflict") {
      return new ApiError(`Upload blocked because${pathLabel} already exists.`, xhr.status, body.error, errorPath);
    }
    if (body.error === "upload_type_conflict") {
      return new ApiError(
        `Upload blocked because${pathLabel} conflicts with an existing file or folder.`,
        xhr.status,
        body.error,
        errorPath,
      );
    }
  } catch {
    // Ignore non-JSON upload failures.
  }

  return new Error("Upload failed");
}

export function buildDownloadUrl(scope: BrowserScope, path: string, tag?: string, csrf?: string | null): string {
  return `/api/download${toQuery({ scope, tag, path, csrf: csrf ?? undefined })}`;
}

export function buildLogDownloadUrl(path: string, csrf?: string | null): string {
  return `/api/logs/download${toQuery({ path, csrf: csrf ?? undefined })}`;
}

export async function getSession(): Promise<SessionResponse> {
  const response = await fetch("/api/session");

  return expectJson<SessionResponse>(response, "Session lookup failed");
}

export async function getStatus(): Promise<StatusResponse> {
  const response = await fetch("/api/status");

  return expectJson<StatusResponse>(response, "Status lookup failed");
}

export async function pairBrowser(code: string, browserId: string): Promise<SessionResponse> {
  const response = await fetch("/api/pair", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ browser_id: browserId, code }).toString(),
  });

  return expectJson<SessionResponse>(response, "Pairing failed");
}

export async function pairBrowserQr(qrToken: string, browserId: string): Promise<SessionResponse> {
  const response = await fetch("/api/pair", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ browser_id: browserId, qr_token: qrToken }).toString(),
  });

  return expectJson<SessionResponse>(response, "QR pairing failed");
}

export async function revokeBrowser(csrf: string): Promise<void> {
  const response = await fetch("/api/revoke", {
    method: "POST",
    headers: {
      "X-CS-CSRF": csrf,
    },
  });

  if (!response.ok) {
    throw new Error("Disconnect failed");
  }
}

export async function getPlatforms(csrf: string): Promise<PlatformsResponse> {
  const response = await fetch("/api/platforms", {
    headers: csrfHeaders(csrf),
  });

  return expectJson<PlatformsResponse>(response, "Platforms lookup failed");
}

export type BrowserRequestOptions = { offset?: number; query?: string; signal?: AbortSignal };

export async function getBrowser(
  scope: BrowserScope,
  csrf: string,
  tag?: string,
  path?: string,
  options?: BrowserRequestOptions,
): Promise<BrowserResponse> {
  const offset = options?.offset && options.offset > 0 ? String(options.offset) : undefined;
  const q = options?.query?.trim() ? options.query.trim() : undefined;
  const response = await fetch(`/api/browser${toQuery({ scope, tag, path, offset, q })}`, {
    headers: csrfHeaders(csrf),
    signal: options?.signal,
  });

  return expectJson<BrowserResponse>(response, "Browser lookup failed");
}

export async function getBrowserAll(
  scope: BrowserScope,
  csrf: string,
  tag?: string,
  path?: string,
  options?: Omit<BrowserRequestOptions, "offset">,
): Promise<BrowserResponse> {
  const firstPage = await getBrowser(scope, csrf, tag, path, {
    query: options?.query,
    signal: options?.signal,
  });
  const entries = [...firstPage.entries];
  let latestPage = firstPage;

  while (entries.length < latestPage.totalCount) {
    const nextPage = await getBrowser(scope, csrf, tag, path, {
      offset: entries.length,
      query: options?.query,
      signal: options?.signal,
    });

    if (nextPage.entries.length === 0) {
      latestPage = nextPage;
      break;
    }

    entries.push(...nextPage.entries);
    latestPage = nextPage;
  }

  return {
    ...firstPage,
    entries,
    totalCount: Math.max(latestPage.totalCount, entries.length),
    truncated: latestPage.truncated,
  };
}

export async function getSaveStates(tag: string, csrf: string): Promise<SaveStatesResponse> {
  const response = await fetch(`/api/states${toQuery({ tag })}`, {
    headers: csrfHeaders(csrf),
  });

  return expectJson<SaveStatesResponse>(response, "Save-state lookup failed");
}

export async function getLogs(csrf: string): Promise<LogsResponse> {
  const response = await fetch("/api/logs", {
    headers: csrfHeaders(csrf),
  });

  return expectJson<LogsResponse>(response, "Logs lookup failed");
}

export async function getMacDotfiles(csrf: string): Promise<MacDotfilesResponse> {
  const response = await fetch("/api/tools/mac-dotfiles", {
    headers: csrfHeaders(csrf),
  });

  return expectJson<MacDotfilesResponse>(response, "Mac dotfile scan failed");
}

export async function searchFiles(path: string | undefined, query: string, csrf: string): Promise<FileSearchResponse> {
  const response = await fetch(`/api/files/search${toQuery({ path, q: query })}`, {
    headers: csrfHeaders(csrf),
  });

  return expectJson<FileSearchResponse>(response, "File search failed");
}

type PreviewUploadOptions = {
  signal?: AbortSignal;
};

export async function previewUpload(
  request: UploadPreviewRequest,
  csrf: string,
  options?: PreviewUploadOptions,
): Promise<UploadPreviewResponse> {
  const form = new FormData();

  form.set("scope", request.scope);
  if (request.tag) {
    form.set("tag", request.tag);
  }
  if (request.path) {
    form.set("path", request.path);
  }
  for (const directory of request.directories ?? []) {
    form.append("directory", directory);
  }
  for (const filePath of request.filePaths) {
    form.append("file_path", filePath);
  }

  const response = await fetch("/api/upload/preview", {
    method: "POST",
    headers: csrfHeaders(csrf),
    body: form,
    signal: options?.signal,
  });

  return expectJson<UploadPreviewResponse>(response, "Upload preview failed");
}

export async function previewUploadBatched(
  request: UploadPreviewRequest,
  csrf: string,
  options?: PreviewUploadOptions,
): Promise<UploadPreviewResponse> {
  const { directories = [], filePaths, ...rest } = request;
  const overwriteable: UploadPreviewResponse["overwriteable"] = [];
  const blocking: UploadPreviewResponse["blocking"] = [];
  const allOverwriteable = new Map<string, UploadPreviewResponse["overwriteable"][number]>();
  const allBlocking = new Map<string, UploadPreviewResponse["blocking"][number]>();

  const appendUnique = <T extends { path: string; kind: string }>(allItems: Map<string, T>, target: T[], items: T[]) => {
    for (const item of items) {
      const key = `${item.kind}:${item.path}`;

      if (allItems.has(key)) {
        continue;
      }
      allItems.set(key, item);
      if (target.length >= UPLOAD_PREVIEW_DISPLAY_LIMIT) {
        continue;
      }
      target.push(item);
    }
  };

  for (let offset = 0; offset < directories.length; offset += UPLOAD_BATCH_SIZE) {
    const batch = directories.slice(offset, offset + UPLOAD_BATCH_SIZE);
    const response = await previewUpload({ ...rest, directories: batch, filePaths: [] }, csrf, options);

    appendUnique(allOverwriteable, overwriteable, response.overwriteable);
    appendUnique(allBlocking, blocking, response.blocking);
  }

  for (let offset = 0; offset < filePaths.length; offset += UPLOAD_BATCH_SIZE) {
    const batch = filePaths.slice(offset, offset + UPLOAD_BATCH_SIZE);
    const response = await previewUpload({ ...rest, directories: [], filePaths: batch }, csrf, options);

    appendUnique(allOverwriteable, overwriteable, response.overwriteable);
    appendUnique(allBlocking, blocking, response.blocking);
  }

  return {
    overwriteableCount: allOverwriteable.size,
    blockingCount: allBlocking.size,
    overwriteable,
    blocking,
  };
}

export function beginUploadFiles(
  request: UploadRequest,
  csrf: string,
  onProgress?: (value: number) => void,
): UploadHandle {
  const xhr = new XMLHttpRequest();
  const promise = new Promise<void>((resolve, reject) => {
    const form = new FormData();
    form.set("scope", request.scope);
    if (request.tag) {
      form.set("tag", request.tag);
    }
    if (request.path) {
      form.set("path", request.path);
    }
    if (request.overwriteExisting) {
      form.set("overwrite", "1");
    }
    for (const directory of request.directories ?? []) {
      form.append("directory", directory);
    }
    for (const file of request.files) {
      form.append("file", file, uploadFileClientPath(file));
    }

    xhr.open("POST", "/api/upload");
    xhr.setRequestHeader("X-CS-CSRF", csrf);
    xhr.upload.addEventListener("progress", (event) => {
      if (event.lengthComputable && onProgress) {
        onProgress(Math.round((event.loaded / event.total) * 100));
      }
    });
    xhr.addEventListener("load", () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
        return;
      }
      reject(parseUploadError(xhr));
    });
    xhr.addEventListener("error", () => reject(new Error("Upload failed")));
    xhr.addEventListener("abort", () => reject(new UploadAbortedError()));
    xhr.send(form);
  });

  return {
    cancel: () => {
      xhr.abort();
    },
    promise,
  };
}

export const UPLOAD_BATCH_SIZE = 32;

function uploadFileClientPath(file: File): string {
  const relativePath = (file as File & { webkitRelativePath?: string }).webkitRelativePath;

  return relativePath && relativePath.length > 0 ? relativePath : file.name;
}

function joinUploadPath(base: string | undefined, child: string): string {
  if (base && child) {
    return `${base}/${child}`;
  }

  return base || child;
}

function uploadedBeforeConflict(batch: File[], basePath: string | undefined, error: unknown): number | null {
  if (!(error instanceof ApiError) || !error.path) {
    return null;
  }

  const conflictIndex = batch.findIndex((file) => joinUploadPath(basePath, uploadFileClientPath(file)) === error.path);

  return conflictIndex >= 0 ? conflictIndex : null;
}

export async function uploadFiles(
  request: UploadRequest,
  csrf: string,
  onProgress?: (value: number) => void,
): Promise<void> {
  const summary = await beginUploadFilesBatched(request, csrf, onProgress).promise;

  if (summary.cancelled) {
    throw new UploadAbortedError();
  }
  if (summary.failed > 0 || summary.directoriesFailed > 0) {
    throw new Error("Upload failed");
  }
}

/* Uploads in batches of UPLOAD_BATCH_SIZE so a folder larger than the server's CS_UPLOAD_MAX_FILES
 * cap still succeeds. Because batches commit progressively, this helper resolves with a summary
 * of what actually landed on disk instead of rejecting — callers MUST refresh their view and
 * report partial state accurately, otherwise retries hit already-written files.
 */
export function beginUploadFilesBatched(
  request: UploadRequest,
  csrf: string,
  onProgress?: (value: number) => void,
): UploadBatchedHandle {
  let cancelled = false;
  let activeHandle: UploadHandle | null = null;

  const promise = (async (): Promise<UploadSummary> => {
    const { files, directories = [], ...rest } = request;
    const totalBytes = files.reduce((sum, f) => sum + f.size, 0);
    const totalWork = totalBytes > 0 ? totalBytes + directories.length : files.length + directories.length;
    let completedWork = 0;
    let uploaded = 0;
    let failed = 0;
    let directoriesCreated = 0;
    let directoriesFailed = 0;
    let errorMessage: string | null = null;

    const reportProgress = (workDone: number) => {
      if (onProgress && totalWork > 0) {
        onProgress(Math.round((workDone / totalWork) * 100));
      }
    };

    for (const directory of directories) {
      if (cancelled) {
        break;
      }

      const handle = beginUploadFiles({ ...rest, files: [], directories: [directory] }, csrf);

      activeHandle = handle;
      try {
        await handle.promise;
        directoriesCreated += 1;
      } catch (error) {
        if (error instanceof UploadAbortedError) {
          cancelled = true;
        } else {
          directoriesFailed += 1;
          errorMessage = error instanceof Error ? error.message : errorMessage;
        }
      } finally {
        activeHandle = null;
      }

      completedWork += 1;
      reportProgress(completedWork);
    }

    for (let offset = 0; offset < files.length; offset += UPLOAD_BATCH_SIZE) {
      if (cancelled) {
        break;
      }

      const batch = files.slice(offset, offset + UPLOAD_BATCH_SIZE);
      const batchBytes = batch.reduce((sum, f) => sum + f.size, 0);

      const handle = beginUploadFiles({ ...rest, files: batch }, csrf, (batchPct) => {
        const batchWork = totalBytes > 0 ? batchBytes : batch.length;
        const batchLoaded = (batchPct / 100) * batchWork;

        reportProgress(completedWork + batchLoaded);
      });

      activeHandle = handle;
      try {
        await handle.promise;
        uploaded += batch.length;
      } catch (error) {
        if (error instanceof UploadAbortedError) {
          cancelled = true;
        } else {
          const partialUploaded = uploadedBeforeConflict(batch, rest.path, error);

          if (partialUploaded === null) {
            failed += batch.length;
          } else {
            uploaded += partialUploaded;
            failed += batch.length - partialUploaded;
          }
          errorMessage = error instanceof Error ? error.message : errorMessage;
        }
      } finally {
        activeHandle = null;
      }

      completedWork += totalBytes > 0 ? batchBytes : batch.length;
      reportProgress(completedWork);
    }

    return { uploaded, failed, directoriesCreated, directoriesFailed, cancelled, errorMessage };
  })();

  return {
    cancel: () => {
      cancelled = true;
      activeHandle?.cancel();
    },
    promise,
  };
}

export async function replaceArt(
  request: ReplaceArtRequest,
  csrf: string,
  onProgress?: (value: number) => void,
): Promise<void> {
  await new Promise<void>((resolve, reject) => {
    const form = new FormData();
    const xhr = new XMLHttpRequest();

    form.set("tag", request.tag);
    form.set("path", request.path);
    form.set("file", request.file, request.file.name);

    xhr.open("POST", "/api/art/replace");
    xhr.setRequestHeader("X-CS-CSRF", csrf);
    xhr.upload.addEventListener("progress", (event) => {
      if (event.lengthComputable && onProgress) {
        onProgress(Math.round((event.loaded / event.total) * 100));
      }
    });
    xhr.addEventListener("load", () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve();
        return;
      }
      reject(new Error("Replace art failed"));
    });
    xhr.addEventListener("error", () => reject(new Error("Replace art failed")));
    xhr.send(form);
  });
}

export async function renameItem(request: RenameRequest, csrf: string): Promise<void> {
  const response = await fetch("/api/item/rename", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "X-CS-CSRF": csrf,
    },
    body: new URLSearchParams({
      scope: request.scope,
      tag: request.tag ?? "",
      from: request.from,
      to: request.to,
    }).toString(),
  });

  if (!response.ok) {
    const errorCode = await readErrorCode(response);

    if (response.status === 409) {
      throw new ApiError("That name is already in use in this folder.", response.status, errorCode ?? "already_exists");
    }
    if (response.status === 404) {
      throw new ApiError("The item you tried to rename no longer exists.", response.status, errorCode ?? "path_not_found");
    }

    throw new ApiError("Rename failed", response.status, errorCode);
  }
}

export async function deleteItem(request: MutationRequest, csrf: string): Promise<void> {
  const response = await fetch("/api/item/delete", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "X-CS-CSRF": csrf,
    },
    body: new URLSearchParams({
      scope: request.scope,
      tag: request.tag ?? "",
      path: request.path,
    }).toString(),
  });

  if (!response.ok) {
    throw new Error("Delete failed");
  }
}

export async function readTextFile(scope: BrowserScope, path: string, csrf: string, tag?: string): Promise<string> {
  const response = await fetch(buildDownloadUrl(scope, path, tag, csrf), {
    headers: csrfHeaders(csrf),
  });

  if (!response.ok) {
    throw new Error("Read failed");
  }

  return response.text();
}

export async function writeTextFile(request: WriteRequest, csrf: string): Promise<void> {
  const response = await fetch(`/api/item/write${toQuery({ scope: request.scope, tag: request.tag, path: request.path })}`, {
    method: "POST",
    headers: {
      "Content-Type": "text/plain; charset=utf-8",
      "X-CS-CSRF": csrf,
    },
    body: request.content,
  });

  if (!response.ok) {
    throw new Error("Write failed");
  }
}

export async function createFolder(request: MutationRequest, csrf: string): Promise<void> {
  const response = await fetch("/api/folder/create", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded",
      "X-CS-CSRF": csrf,
    },
    body: new URLSearchParams({
      scope: request.scope,
      tag: request.tag ?? "",
      path: request.path,
    }).toString(),
  });

  if (!response.ok) {
    throw new Error("Create folder failed");
  }
}

export async function createTerminalSession(csrf: string): Promise<TerminalSessionResponse> {
  const response = await fetch("/api/terminal/session", {
    method: "POST",
    headers: {
      "X-CS-CSRF": csrf,
    },
  });

  return expectJson<TerminalSessionResponse>(response, "Terminal session failed");
}
