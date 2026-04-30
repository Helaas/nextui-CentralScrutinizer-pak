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
  UploadRequest,
  WriteRequest,
} from "./types";

export class ApiError extends Error {
  code?: string;
  status: number;

  constructor(message: string, status: number, code?: string) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
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
  cancelled: boolean;
};

export type UploadBatchedHandle = {
  cancel: () => void;
  promise: Promise<UploadSummary>;
};

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

export async function getBrowser(scope: BrowserScope, csrf: string, tag?: string, path?: string): Promise<BrowserResponse> {
  const response = await fetch(`/api/browser${toQuery({ scope, tag, path })}`, {
    headers: csrfHeaders(csrf),
  });

  return expectJson<BrowserResponse>(response, "Browser lookup failed");
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
    for (const file of request.files) {
      const relativePath = (file as File & { webkitRelativePath?: string }).webkitRelativePath;

      form.append("file", file, relativePath && relativePath.length > 0 ? relativePath : file.name);
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
      reject(new Error("Upload failed"));
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

export async function uploadFiles(
  request: UploadRequest,
  csrf: string,
  onProgress?: (value: number) => void,
): Promise<void> {
  const summary = await beginUploadFilesBatched(request, csrf, onProgress).promise;

  if (summary.cancelled) {
    throw new UploadAbortedError();
  }
  if (summary.failed > 0) {
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
    const { files, ...rest } = request;
    const totalBytes = files.reduce((sum, f) => sum + f.size, 0);
    let uploadedBytes = 0;
    let uploaded = 0;
    let failed = 0;

    for (let offset = 0; offset < files.length; offset += UPLOAD_BATCH_SIZE) {
      if (cancelled) {
        break;
      }

      const batch = files.slice(offset, offset + UPLOAD_BATCH_SIZE);
      const batchBytes = batch.reduce((sum, f) => sum + f.size, 0);

      const handle = beginUploadFiles({ ...rest, files: batch }, csrf, (batchPct) => {
        if (onProgress && totalBytes > 0) {
          const batchLoaded = (batchPct / 100) * batchBytes;
          onProgress(Math.round(((uploadedBytes + batchLoaded) / totalBytes) * 100));
        }
      });

      activeHandle = handle;
      try {
        await handle.promise;
        uploaded += batch.length;
      } catch (error) {
        if (error instanceof UploadAbortedError) {
          cancelled = true;
        } else {
          failed += batch.length;
        }
      } finally {
        activeHandle = null;
      }

      uploadedBytes += batchBytes;
      if (onProgress && totalBytes > 0) {
        onProgress(Math.round((uploadedBytes / totalBytes) * 100));
      }
    }

    return { uploaded, failed, cancelled };
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
