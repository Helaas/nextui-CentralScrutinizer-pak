"use client";

import { useEffect, useMemo, useRef, useState } from "react";

import { AppShell } from "../components/app-shell";
import { BrowserView } from "../components/browser-view";
import { DashboardShell } from "../components/dashboard-shell";
import { FileEditorModal } from "../components/file-editor-modal";
import { ZipExtractDialog } from "../components/zip-extract-dialog";
import { LogsToolView } from "../components/logs-tool-view";
import { MacDotCleanToolView } from "../components/mac-dot-clean-tool-view";
import { PairScreen } from "../components/pair-screen";
import { PlatformView } from "../components/platform-view";
import { CollectionsToolView } from "../components/collections-tool-view";
import { SaveStatesView } from "../components/save-states-view";
import { ScreenshotsToolView } from "../components/screenshots-tool-view";
import { TerminalToolView } from "../components/terminal-tool-view";
import { ToolsView } from "../components/tools-view";
import {
  ApiError,
  PAIRING_UNAVAILABLE_MESSAGE,
  beginUploadFilesBatched,
  createFolder,
  deleteItem,
  getPlatforms,
  getSession,
  pairBrowser,
  pairBrowserQr,
  previewUploadBatched,
  readTextFile,
  replaceArt,
  renameItem,
  revokeBrowser,
  searchFiles,
  writeTextFile,
} from "../lib/api";
import { getBrowserId } from "../lib/browser-id";
import { parseZipFile, uploadPathsFromZip, uploadSelectionFromZip, type ParsedZipPreview } from "../lib/zip-upload";
import { useBrowserPagination } from "../lib/use-browser-pagination";
import {
  createPlatformDisplayNames,
  filterPlatformGroups,
  flattenPlatformGroups,
  formatPlatformDescription,
  platformSupportsBrowserScope,
  platformSupportsResource,
} from "../lib/platform-display";
import {
  getDestination,
  readLibraryEmuFilter,
  readShowEmptyPlatforms,
  readViewState,
  type AppViewState,
  writeViewState,
} from "../lib/navigation";
import { PLAINTEXT_MAX_BYTES } from "../lib/plaintext";
import type {
  BrowserEntry,
  BrowserScope,
  ExtractStrategy,
  FileSearchResult,
  LibraryEmuFilter,
  PlatformGroup,
  PlatformSummary,
  PlatformResource,
  SessionResponse,
  TransferState,
  UploadPreviewResponse,
  UploadSelection,
  ZipExtractOptions,
} from "../lib/types";

type DirectoryCapableInput = HTMLInputElement & { webkitdirectory?: boolean };
const ZIP_PREVIEW_TIMEOUT_MS = 15_000;

function getInitialView(): AppViewState {
  if (typeof window === "undefined") {
    return { view: "dashboard", destination: "library" };
  }

  return readViewState(window.location.search);
}

function findPlatform(groups: PlatformGroup[], tag: string): PlatformSummary | undefined {
  return flattenPlatformGroups(groups).find((platform) => platform.tag === tag);
}

function joinRelativePath(base: string | undefined, name: string): string {
  return base ? `${base}/${name}` : name;
}

function buildRenamedPath(existingPath: string, nextName: string): string {
  const lastSlash = existingPath.lastIndexOf("/");

  return lastSlash >= 0 ? `${existingPath.slice(0, lastSlash)}/${nextName}` : nextName;
}

function normalizeRelativeDirectory(path: string): string {
  return path.trim().replace(/^\/+|\/+$/g, "");
}

function buildMovedPath(entry: BrowserEntry, destinationPath: string): string {
  const normalized = normalizeRelativeDirectory(destinationPath);

  return normalized ? `${normalized}/${entry.name}` : entry.name;
}

async function pickSingleFile(accept?: string): Promise<File | null> {
  return new Promise((resolve) => {
    const input = document.createElement("input");

    input.type = "file";
    if (accept) {
      input.accept = accept;
    }
    input.addEventListener("change", () => {
      resolve(input.files?.[0] ?? null);
    });
    input.click();
  });
}

function browserSupportsDirectoryUpload(): boolean {
  if (typeof document === "undefined") {
    return false;
  }

  return "webkitdirectory" in (document.createElement("input") as DirectoryCapableInput);
}

async function pickFolderFiles(): Promise<UploadSelection> {
  return new Promise((resolve) => {
    const input = document.createElement("input") as DirectoryCapableInput;

    input.type = "file";
    input.multiple = true;
    input.webkitdirectory = true;
    input.addEventListener("change", () => {
      resolve({ directories: [], files: Array.from(input.files ?? []) });
    });
    input.click();
  });
}

function hasUploadItems(selection: UploadSelection): boolean {
  return selection.files.length > 0 || selection.directories.length > 0;
}

function formatUploadCount(count: number, singular: string): string | null {
  return count > 0 ? `${count} ${singular}${count === 1 ? "" : "s"}` : null;
}

function formatUploadParts(files: number, directories: number): string {
  return [formatUploadCount(files, "file"), formatUploadCount(directories, "folder")]
    .filter((part): part is string => Boolean(part))
    .join(" and ");
}

function formatItemCount(count: number): string {
  return `${count} item${count === 1 ? "" : "s"}`;
}

function normalizeSession(
  session: Partial<SessionResponse> | null | undefined,
): SessionResponse {
  return {
    paired: session?.paired === true,
    csrf: typeof session?.csrf === "string" ? session.csrf : null,
    trustedCount: typeof session?.trustedCount === "number" ? session.trustedCount : 0,
    pairingAvailable: session?.pairingAvailable !== false,
    capabilities: {
      terminal: session?.capabilities?.terminal === true,
    },
  };
}

function emptySession(): SessionResponse {
  return {
    paired: false,
    csrf: null,
    trustedCount: 0,
    pairingAvailable: true,
    capabilities: {
      terminal: false,
    },
  };
}

function getPairErrorMessage(error: unknown): string {
  return error instanceof Error ? error.message : "Pairing failed";
}

function getReconnectMessage(): string {
  return "Connection restored, but this browser is no longer trusted. Refresh the PIN or QR code on the device to pair again.";
}

function isAbortError(error: unknown): boolean {
  return error instanceof DOMException && error.name === "AbortError";
}

function getPairingUnavailableMessage(): string {
  return PAIRING_UNAVAILABLE_MESSAGE;
}

function isFileBrowserTool(
  viewState: AppViewState,
): viewState is { view: "tools"; destination: "tools"; tool: "file-browser"; path?: string } {
  return viewState.view === "tools" && viewState.tool === "file-browser";
}

export default function Page() {
  type ShellSearchKey = "library" | "browser" | "file-browser";

  const [browserId] = useState(() => (typeof window === "undefined" ? "browser-server-render" : getBrowserId()));
  const [session, setSession] = useState<SessionResponse | null>(null);
  const [viewState, setViewState] = useState<AppViewState>(getInitialView);
  const [showEmptyPlatforms, setShowEmptyPlatforms] = useState(() =>
    typeof window === "undefined" ? false : readShowEmptyPlatforms(window.location.search),
  );
  const [libraryEmuFilter, setLibraryEmuFilter] = useState<LibraryEmuFilter>(() =>
    typeof window === "undefined" ? "installed" : readLibraryEmuFilter(window.location.search),
  );
  const [platformGroups, setPlatformGroups] = useState<PlatformGroup[]>([]);
  const [fileSearchResults, setFileSearchResults] = useState<FileSearchResult[] | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [pairError, setPairError] = useState<string | null>(null);
  const [pairMessage, setPairMessage] = useState<string | null>(null);
  const [isBusy, setIsBusy] = useState(false);
  const [isPairing, setIsPairing] = useState(false);
  const [connectionLost, setConnectionLost] = useState(false);
  const [retryUnavailableSession, setRetryUnavailableSession] = useState(false);
  const [transfer, setTransfer] = useState<TransferState>({ active: false, label: "", progress: 0 });
  const [canUploadFolder, setCanUploadFolder] = useState(false);
  const [searchByContext, setSearchByContext] = useState<Record<ShellSearchKey, string>>({
    library: "",
    browser: "",
    "file-browser": "",
  });
  const [editor, setEditor] = useState<{
    entry: BrowserEntry;
    content: string;
    loading: boolean;
    loadError: string | null;
    saving: boolean;
  } | null>(null);
  const [zipExtractDialog, setZipExtractDialog] = useState<{
    preview: ParsedZipPreview;
    file: File;
    strategy: ExtractStrategy;
    overwriteExisting: boolean;
    preflight: UploadPreviewResponse | null;
    checking: boolean;
  } | null>(null);
  const zipPreviewAbortRef = useRef<{
    controller: AbortController;
    timeoutId: ReturnType<typeof setTimeout>;
    timedOut: boolean;
  } | null>(null);

  function clearZipPreviewAbort(abort = true) {
    const activeRequest = zipPreviewAbortRef.current;

    if (!activeRequest) {
      return;
    }
    clearTimeout(activeRequest.timeoutId);
    zipPreviewAbortRef.current = null;
    if (abort) {
      activeRequest.controller.abort();
    }
  }

  function beginZipPreviewAbort() {
    const controller = new AbortController();
    const activeRequest = {
      controller,
      timeoutId: setTimeout(() => {
        activeRequest.timedOut = true;
        controller.abort();
      }, ZIP_PREVIEW_TIMEOUT_MS),
      timedOut: false,
    };

    clearZipPreviewAbort();
    zipPreviewAbortRef.current = activeRequest;
    return activeRequest;
  }

  useEffect(() => {
    return () => {
      clearZipPreviewAbort();
    };
  }, []);

  function navigate(next: AppViewState, replace = false) {
    setViewState(next);
    setNotice(null);
    if (typeof window !== "undefined") {
      const url = writeViewState(next, { showEmptyPlatforms, libraryEmuFilter });

      if (replace) {
        window.history.replaceState(null, "", url);
      } else {
        window.history.pushState(null, "", url);
      }
    }
  }

  async function loadPlatforms(currentCsrf = session?.csrf): Promise<PlatformGroup[]> {
    if (!currentCsrf) {
      setPlatformGroups([]);
      return [];
    }

    const response = await getPlatforms(currentCsrf);

    setPlatformGroups(response.groups);
    return response.groups;
  }

  async function refreshSessionState(): Promise<SessionResponse> {
    const nextSession = normalizeSession(await getSession());

    setSession(nextSession);
    setConnectionLost(false);
    setRetryUnavailableSession(false);
    return nextSession;
  }

  const browserContext = useMemo<{ scope: BrowserScope; tag?: string; path?: string } | null>(() => {
    if (!session?.csrf) {
      return null;
    }
    if (viewState.view === "browser") {
      const platform = findPlatform(platformGroups, viewState.tag);

      if (!platform || !platformSupportsBrowserScope(platform, viewState.scope)) {
        return null;
      }
      return { scope: viewState.scope, tag: viewState.tag, path: viewState.path };
    }
    if (isFileBrowserTool(viewState)) {
      return { scope: "files", path: viewState.path };
    }
    return null;
  }, [session?.csrf, viewState, platformGroups]);

  const browserSearchKey: ShellSearchKey = isFileBrowserTool(viewState) ? "file-browser" : "browser";
  const browserSearchValue = searchByContext[browserSearchKey];

  const browser = useBrowserPagination({
    scope: browserContext?.scope ?? null,
    tag: browserContext?.tag,
    path: browserContext?.path,
    search: browserSearchValue,
    csrf: session?.csrf,
    enabled: browserContext !== null,
  });

  const browserRefreshRef = useRef(browser.refresh);
  browserRefreshRef.current = browser.refresh;

  async function refreshCurrentData(currentCsrf = session?.csrf) {
    await loadPlatforms(currentCsrf);
    await browserRefreshRef.current();
  }

  const browserResponse = useMemo(
    () => (browser.metadata ? { ...browser.metadata, entries: browser.entries } : null),
    [browser.metadata, browser.entries],
  );

  function clearTransfer() {
    setTransfer({ active: false, label: "", progress: 0 });
  }

  function currentScopeState(currentView = viewState):
    | { scope: BrowserScope; tag?: string; path?: string }
    | null {
    if (currentView.view === "browser") {
      return { scope: currentView.scope, tag: currentView.tag, path: currentView.path };
    }
    if (isFileBrowserTool(currentView)) {
      return { scope: "files", path: currentView.path };
    }

    return null;
  }

  useEffect(() => {
    const handlePopState = () => {
      setViewState(readViewState(window.location.search));
      setShowEmptyPlatforms(readShowEmptyPlatforms(window.location.search));
      setLibraryEmuFilter(readLibraryEmuFilter(window.location.search));
      setNotice(null);
    };

    window.addEventListener("popstate", handlePopState);
    return () => {
      window.removeEventListener("popstate", handlePopState);
    };
  }, []);

  useEffect(() => {
    let active = true;

    void (async () => {
      try {
        const params = new URLSearchParams(window.location.search);
        const qrToken = params.get("pairQr");

        if (qrToken) {
          setPairError(null);
          setPairMessage(null);
          setIsPairing(true);

          try {
            const pairedSession = normalizeSession(await pairBrowserQr(qrToken, browserId));

            if (!active) {
              return;
            }

            setConnectionLost(false);
            setRetryUnavailableSession(false);
            setSession(pairedSession);
            navigate({ view: "dashboard", destination: "library" }, true);
            await refreshCurrentData(pairedSession.csrf);
            return;
          } catch (error) {
            if (!active) {
              return;
            }

            if (error instanceof ApiError && error.code === "pairing_unavailable") {
              setSession({
                ...emptySession(),
                pairingAvailable: false,
              });
              setPairError(null);
              setPairMessage(getPairingUnavailableMessage());
              navigate({ view: "pair" }, true);
              return;
            }

            setPairError(getPairErrorMessage(error));
            navigate({ view: "pair" }, true);
          } finally {
            if (active) {
              setIsPairing(false);
            }
          }
        }

        const nextSession = normalizeSession(await getSession());

        if (!active) {
          return;
        }

        setConnectionLost(false);
        setRetryUnavailableSession(false);
        setSession(nextSession);
        if (!nextSession.paired) {
          setPairMessage(nextSession.pairingAvailable ? null : getPairingUnavailableMessage());
          navigate({ view: "pair" }, true);
          return;
        }

        if (viewState.view === "pair") {
          navigate({ view: "dashboard", destination: "library" }, true);
          await refreshCurrentData(nextSession.csrf);
          return;
        }

        await refreshCurrentData(nextSession.csrf);
      } catch {
        if (active) {
          setConnectionLost(false);
          setRetryUnavailableSession(true);
          setSession(emptySession());
          setPairMessage("Connection to the device is unavailable. Open the device app on the handheld to continue.");
        }
      }
    })();

    return () => {
      active = false;
    };
  }, []);

  useEffect(() => {
    if (viewState.view !== "browser" && viewState.view !== "states") {
      return;
    }

    const platform = findPlatform(platformGroups, viewState.tag);
    if (!platform) {
      return;
    }

    if (
      (viewState.view === "browser" && !platformSupportsBrowserScope(platform, viewState.scope)) ||
      (viewState.view === "states" && !platformSupportsResource(platform, "states"))
    ) {
      navigate({ view: "platform", destination: "library", tag: platform.tag }, true);
    }
  }, [platformGroups, viewState]);

  useEffect(() => {
    if (!isFileBrowserTool(viewState)) {
      setFileSearchResults(null);
    }
  }, [viewState]);

  useEffect(() => {
    if (!session?.paired || viewState.view !== "tools") {
      return;
    }

    let active = true;

    void getSession()
      .then((rawSession) => {
        if (active && rawSession) {
          setSession(normalizeSession(rawSession));
        }
      })
      .catch(() => {
        // Ignore transient refresh failures and keep the last known capabilities.
      });

    return () => {
      active = false;
    };
  }, [session?.paired, viewState.view, viewState.view === "tools" ? viewState.tool : undefined]);

  useEffect(() => {
    setCanUploadFolder(browserSupportsDirectoryUpload());
  }, []);

  useEffect(() => {
    if (!session?.paired && !retryUnavailableSession) {
      setConnectionLost(false);
      return;
    }

    let active = true;
    let timer: number | undefined;

    const schedule = (delayMs: number) => {
      timer = window.setTimeout(() => {
        void pollSession();
      }, delayMs);
    };

    const pollSession = async () => {
      try {
        const nextSession = normalizeSession(await getSession());

        if (!active) {
          return;
        }

        if (!nextSession.paired) {
          setConnectionLost(false);
          setRetryUnavailableSession(false);
          setSession(nextSession);
          setPairError(null);
          setPairMessage(
            nextSession.pairingAvailable
              ? retryUnavailableSession
                ? null
                : getReconnectMessage()
              : getPairingUnavailableMessage(),
          );
          navigate({ view: "pair" }, true);
          return;
        }

        setSession(nextSession);
        setPairError(null);
        setPairMessage(null);
        if (viewState.view === "pair") {
          setConnectionLost(false);
          setRetryUnavailableSession(false);
          navigate({ view: "dashboard", destination: "library" }, true);
          await refreshCurrentData(nextSession.csrf);
          return;
        }
        if (connectionLost || retryUnavailableSession) {
          setConnectionLost(false);
          setRetryUnavailableSession(false);
          await refreshCurrentData(nextSession.csrf);
        }
        schedule(5000);
      } catch {
        if (!active) {
          return;
        }

        if (session?.paired) {
          setConnectionLost(true);
        }
        schedule(2000);
      }
    };

    schedule(connectionLost || retryUnavailableSession ? 2000 : 5000);
    return () => {
      active = false;
      if (typeof timer === "number") {
        window.clearTimeout(timer);
      }
    };
  }, [connectionLost, retryUnavailableSession, session?.paired, viewState]);

  async function withBusy(action: () => Promise<void>) {
    setIsBusy(true);
    try {
      await action();
    } finally {
      setIsBusy(false);
    }
  }

  if (!session) {
    return <main className="flex min-h-screen items-center justify-center text-sm text-[var(--muted)]">Loading...</main>;
  }

  if (!session.paired) {
    return (
      <PairScreen
        error={pairError}
        isBusy={isPairing}
        message={pairMessage}
        pairingAvailable={session.pairingAvailable}
        onSubmit={async (code) => {
          setPairError(null);
          setPairMessage(null);
          setIsPairing(true);
          try {
            const nextSession = await pairBrowser(code, browserId);

            const pairedSession = normalizeSession(nextSession);

            setConnectionLost(false);
            setRetryUnavailableSession(false);
            setSession(pairedSession);
            navigate({ view: "dashboard", destination: "library" }, true);
            await refreshCurrentData(pairedSession.csrf);
          } catch (error) {
            if (error instanceof ApiError && error.code === "pairing_unavailable") {
              setSession((current) => ({
                ...(current ?? emptySession()),
                paired: false,
                csrf: null,
                pairingAvailable: false,
              }));
              setPairError(null);
              setPairMessage(getPairingUnavailableMessage());
              return;
            }
            setPairError(getPairErrorMessage(error));
          } finally {
            setIsPairing(false);
          }
        }}
      />
    );
  }

  const activePlatform =
    viewState.view === "platform" || viewState.view === "states" || viewState.view === "browser"
      ? findPlatform(platformGroups, viewState.tag)
      : undefined;
  const visiblePlatformGroups = filterPlatformGroups(
    platformGroups,
    searchByContext.library,
    libraryEmuFilter,
    showEmptyPlatforms,
  );
  const visiblePlatformDisplayNames = createPlatformDisplayNames(
    flattenPlatformGroups(visiblePlatformGroups),
  );
  const activePlatformDisplayName = activePlatform
    ? visiblePlatformDisplayNames.get(activePlatform.tag) ?? activePlatform.name
    : undefined;
  const fallbackToPlatformView =
    !!activePlatform &&
    ((viewState.view === "browser" && !platformSupportsBrowserScope(activePlatform, viewState.scope)) ||
      (viewState.view === "states" && !platformSupportsResource(activePlatform, "states")));
  const showPlatformView =
    !!activePlatform && (viewState.view === "platform" || fallbackToPlatformView);
  const showStatesView =
    viewState.view === "states" && !!activePlatform && platformSupportsResource(activePlatform, "states");
  const showManagedBrowserView =
    viewState.view === "browser" && !!activePlatform && platformSupportsBrowserScope(activePlatform, viewState.scope);
  const terminalEnabled = session.capabilities?.terminal ?? false;

  const handleDisconnect = () => {
    void withBusy(async () => {
      if (!session.csrf) {
        return;
      }
      await revokeBrowser(session.csrf);
      setConnectionLost(false);
      setRetryUnavailableSession(false);
      setPairError(null);
      setPairMessage(null);
      setSession(emptySession());
      navigate({ view: "pair" }, true);
    });
  };

  function navigateToDestination(destination: "library" | "tools") {
    if (destination === "library") {
      navigate({ view: "dashboard", destination: "library" });
      return;
    }

    navigate({ view: "tools", destination: "tools" });
  }

  function updateSearch(key: ShellSearchKey, value: string) {
    setSearchByContext((current) => ({ ...current, [key]: value }));
  }

  function updateShowEmpty(value: boolean) {
    setShowEmptyPlatforms(value);
    if (typeof window !== "undefined" && getDestination(viewState) === "library") {
      window.history.replaceState(null, "", writeViewState(viewState, { showEmptyPlatforms: value, libraryEmuFilter }));
    }
  }

  function updateLibraryEmuFilter(value: LibraryEmuFilter) {
    setLibraryEmuFilter(value);
    if (typeof window !== "undefined" && getDestination(viewState) === "library") {
      window.history.replaceState(null, "", writeViewState(viewState, { showEmptyPlatforms, libraryEmuFilter: value }));
    }
  }

  const handleUploadSelection = async (selection: UploadSelection, options?: { overwriteExisting?: boolean }) => {
    const scopeState = currentScopeState();
    const csrf = session.csrf;

    if (!scopeState || !csrf || !hasUploadItems(selection)) {
      return;
    }

    await withBusy(async () => {
      const totalFiles = selection.files.length;
      const totalDirectories = selection.directories.length;
      const label = `Uploading ${formatUploadParts(totalFiles, totalDirectories)}`;
      const upload = beginUploadFilesBatched({ ...scopeState, ...selection, overwriteExisting: options?.overwriteExisting }, csrf, (progress) => {
        setTransfer((current) => ({ ...current, progress }));
      });

      setTransfer({
        active: true,
        cancellable: true,
        label,
        onCancel: upload.cancel,
        progress: 0,
      });
      try {
        const summary = await upload.promise;

        /* Refresh regardless of outcome — earlier batches may have committed before a later
         * batch failed or the user cancelled, so the browser view is out of sync either way.
         */
        await refreshCurrentData();

        const uploadedParts = formatUploadParts(summary.uploaded, summary.directoriesCreated);
        const failedParts = formatUploadParts(summary.failed, summary.directoriesFailed);
        const noneUploaded = summary.uploaded === 0 && summary.directoriesCreated === 0;
        const anyFailed = summary.failed > 0 || summary.directoriesFailed > 0;

        if (summary.cancelled && noneUploaded) {
          setNotice("Upload cancelled.");
        } else if (summary.cancelled) {
          setNotice(`Upload cancelled after ${uploadedParts}.`);
        } else if (anyFailed && noneUploaded && summary.errorMessage) {
          setNotice(summary.errorMessage);
        } else if (anyFailed && noneUploaded) {
          setNotice(`Upload failed${failedParts ? ` (${failedParts} failed)` : ""}.`);
        } else if (anyFailed) {
          setNotice(
            summary.errorMessage
              ? `Uploaded ${uploadedParts}, ${failedParts} failed. ${summary.errorMessage}`
              : `Uploaded ${uploadedParts}, ${failedParts} failed.`,
          );
        } else {
          setNotice(`Uploaded ${uploadedParts}.`);
        }
      } finally {
        clearTransfer();
      }
    });
  };

  const handleUploadFolder = async () => {
    const scopeState = currentScopeState();

    if (!scopeState || (scopeState.scope !== "roms" && scopeState.scope !== "files")) {
      return;
    }

    const selection = await pickFolderFiles();

    if (hasUploadItems(selection)) {
      await handleUploadSelection(selection);
    }
  };

  const handleUploadZip = async () => {
    const scopeState = currentScopeState();

    if (!scopeState || (scopeState.scope !== "roms" && scopeState.scope !== "files")) {
      return;
    }

    const file = await pickSingleFile(".zip,.pakz,application/zip,application/x-zip-compressed");

    if (!file) {
      return;
    }

    try {
      const preview = await parseZipFile(file);

      if (preview.entries.length === 0) {
        setNotice("ZIP contains no uploadable files or folders.");
        return;
      }

      setZipExtractDialog({
        preview,
        file,
        strategy: "extract-into-folder",
        overwriteExisting: false,
        preflight: null,
        checking: false,
      });
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "ZIP upload failed.");
    }
  };

  const handleConfirmZipExtract = async ({ strategy, overwriteExisting }: ZipExtractOptions) => {
    if (!zipExtractDialog || !session.csrf) {
      return;
    }

    const scopeState = currentScopeState();

    if (!scopeState) {
      return;
    }

    const { preview } = zipExtractDialog;
    const uploadPaths = uploadPathsFromZip(preview, strategy);

    if (uploadPaths.filePaths.length === 0 && uploadPaths.directories.length === 0) {
      setNotice("ZIP contains no uploadable files or folders.");
      return;
    }

    setZipExtractDialog((current) =>
      current
        ? {
            ...current,
            strategy,
            overwriteExisting,
            checking: true,
            preflight: null,
          }
        : current,
    );

    const previewRequest = beginZipPreviewAbort();

    try {
      const preflight = await previewUploadBatched(
        {
          ...scopeState,
          directories: uploadPaths.explicitDirectories,
          filePaths: uploadPaths.filePaths,
        },
        session.csrf,
        { signal: previewRequest.controller.signal },
      );
      if (zipPreviewAbortRef.current === previewRequest) {
        clearTimeout(previewRequest.timeoutId);
        zipPreviewAbortRef.current = null;
      }

      if (preflight.blockingCount > 0 || (!overwriteExisting && preflight.overwriteableCount > 0)) {
        setZipExtractDialog((current) =>
          current
            ? {
                ...current,
                strategy,
                overwriteExisting,
                checking: false,
                preflight,
              }
            : current,
        );
        return;
      }

      const selection = await uploadSelectionFromZip(preview, strategy);

      if (!hasUploadItems(selection)) {
        setZipExtractDialog((current) => (current ? { ...current, checking: false } : current));
        setNotice("ZIP contains no uploadable files or folders.");
        return;
      }

      setZipExtractDialog(null);
      await handleUploadSelection(selection, { overwriteExisting });
    } catch (error) {
      const isCurrentPreviewRequest = zipPreviewAbortRef.current === previewRequest;
      const timedOut = isCurrentPreviewRequest && previewRequest.timedOut;
      const abortError = isAbortError(error);

      if (isCurrentPreviewRequest) {
        clearZipPreviewAbort(false);
        setZipExtractDialog((current) => (current ? { ...current, checking: false } : current));
      } else if (!abortError) {
        setZipExtractDialog((current) => (current ? { ...current, checking: false } : current));
      }
      if (abortError) {
        if (timedOut) {
          setNotice("ZIP conflict check timed out.");
        }
        return;
      }
      setNotice(error instanceof Error ? error.message : "ZIP upload failed.");
    }
  };

  const handleRunFileSearch = async () => {
    if (!isFileBrowserTool(viewState)) {
      return;
    }

    const query = searchByContext["file-browser"].trim();

    if (!query) {
      setFileSearchResults(null);
      return;
    }

    await withBusy(async () => {
      if (!session.csrf) {
        return;
      }

      const response = await searchFiles(viewState.path, query, session.csrf);

      setFileSearchResults(response.results);
      setNotice(response.truncated ? "Search results truncated at 200 matches." : null);
    });
  };

  const handleReplaceArt = async (entry: BrowserEntry) => {
    const csrf = session.csrf;

    if (viewState.view !== "browser" || viewState.scope !== "roms" || !csrf) {
      return;
    }

    const file = await pickSingleFile(".png");
    if (!file) {
      return;
    }
    if (!file.name.toLowerCase().endsWith(".png")) {
      setNotice("Artwork must be uploaded as a PNG file.");
      return;
    }

    await withBusy(async () => {
      setTransfer({ active: true, label: `Updating artwork for ${entry.name}`, progress: 0 });
      try {
        await replaceArt(
          {
            tag: viewState.tag,
            path: entry.path,
            file,
          },
          csrf,
          (progress) => {
            setTransfer((current) => ({ ...current, progress }));
          },
        );
        await refreshCurrentData();
        setNotice(`Artwork updated for ${entry.name}.`);
      } catch (error) {
        setNotice(error instanceof Error ? error.message : "Artwork update failed.");
      } finally {
        clearTransfer();
      }
    });
  };

  const handleCreateFolder = async () => {
    const scopeState = currentScopeState();
    const name = window.prompt("Folder name");
    const csrf = session.csrf;

    if (!scopeState || !csrf || !name) {
      return;
    }

    if (scopeState.scope !== "roms" && scopeState.scope !== "files") {
      return;
    }

    await withBusy(async () => {
      await createFolder(
        {
          ...scopeState,
          path: joinRelativePath(scopeState.path, name.trim()),
        },
        csrf,
      );
      await refreshCurrentData();
      setNotice(`Created folder ${name.trim()}.`);
    });
  };

  const handleRename = async (entry: BrowserEntry) => {
    const scopeState = currentScopeState();
    const nextName = window.prompt("Rename item", entry.name);
    const csrf = session.csrf;
    const trimmedName = nextName?.trim();
    const nameInUse = browser.entries.some(
      (candidate) =>
        candidate.path !== entry.path && candidate.name.toLocaleLowerCase() === (trimmedName ?? "").toLocaleLowerCase(),
    );

    if (!scopeState || !csrf || !trimmedName || trimmedName === entry.name) {
      return;
    }
    if (nameInUse) {
      setNotice(`Can't rename ${entry.name} to ${trimmedName} because that name is already in use.`);
      return;
    }

    await withBusy(async () => {
      try {
        await renameItem(
          {
            scope: scopeState.scope,
            tag: scopeState.tag,
            from: entry.path,
            to: buildRenamedPath(entry.path, trimmedName),
          },
          csrf,
        );
        await refreshCurrentData();
        setNotice(`Renamed ${entry.name} to ${trimmedName}.`);
      } catch (error) {
        setNotice(error instanceof Error ? error.message : "Rename failed.");
      }
    });
  };

  const handleMoveSelection = async (entries: BrowserEntry[], destinationPath: string) => {
    const scopeState = currentScopeState();
    const csrf = session.csrf;
    const normalizedDestination = normalizeRelativeDirectory(destinationPath);
    const moves = entries
      .map((entry) => ({ entry, to: buildMovedPath(entry, normalizedDestination) }))
      .filter(
        ({ entry, to }) =>
          to !== entry.path &&
          !(entry.type === "directory" && (to === entry.path || to.startsWith(`${entry.path}/`))),
      );

    if (!scopeState || !csrf || entries.length === 0) {
      return;
    }
    if (moves.length === 0) {
      setNotice("Nothing to move.");
      return;
    }

    await withBusy(async () => {
      const total = moves.length;
      let completed = 0;

      setTransfer({ active: true, label: `Moving ${formatItemCount(total)}...`, progress: 0 });
      try {
        const results = await Promise.allSettled(
          moves.map(async ({ entry, to }) => {
            try {
              return await renameItem(
                {
                  scope: scopeState.scope,
                  tag: scopeState.tag,
                  from: entry.path,
                  to,
                },
                csrf,
              );
            } finally {
              completed++;
              setTransfer({
                active: true,
                label: `Moving ${completed} of ${total} item${total === 1 ? "" : "s"}...`,
                progress: Math.round((completed / total) * 100),
              });
            }
          }),
        );
        const successCount = results.filter((result) => result.status === "fulfilled").length;
        const failureCount = moves.length - successCount;
        const destinationLabel = normalizedDestination || "SD Card root";

        await refreshCurrentData();
        if (failureCount === 0) {
          setNotice(`Moved ${formatItemCount(successCount)} to ${destinationLabel}.`);
          return;
        }
        if (successCount === 0) {
          setNotice(`Failed to move ${formatItemCount(moves.length)}.`);
          return;
        }

        setNotice(`Moved ${successCount} of ${moves.length} items to ${destinationLabel}. ${failureCount} failed.`);
      } finally {
        clearTransfer();
      }
    });
  };

  const openFileEditor = (entry: BrowserEntry) => {
    setEditor({ entry, content: "", loading: true, loadError: null, saving: false });

    void (async () => {
      try {
        if (!session.csrf) {
          throw new Error("Missing session csrf token.");
        }

        const content = await readTextFile("files", entry.path, session.csrf);

        setEditor((current) => {
          if (!current || current.entry.path !== entry.path) {
            return current;
          }

          return { ...current, content, loading: false };
        });
      } catch {
        setEditor((current) => {
          if (!current || current.entry.path !== entry.path) {
            return current;
          }

          return { ...current, loading: false, loadError: "Could not load file contents." };
        });
      }
    })();
  };

  const handleSaveEditor = async (nextContent: string) => {
    const csrf = session.csrf;

    if (!editor || !csrf) {
      return;
    }

    const byteLength = new TextEncoder().encode(nextContent).length;

    if (byteLength > PLAINTEXT_MAX_BYTES) {
      return;
    }

    setEditor((current) => (current ? { ...current, saving: true } : current));

    try {
      await writeTextFile(
        { scope: "files", path: editor.entry.path, content: nextContent },
        csrf,
      );
      setEditor(null);
      await withBusy(async () => {
        await refreshCurrentData();
        setNotice(`Saved ${editor.entry.name}.`);
      });
    } catch {
      setEditor((current) =>
        current
          ? { ...current, saving: false, loadError: "Save failed. Please try again." }
          : current,
      );
    }
  };

  const handleDeleteSelection = async (entries: BrowserEntry[]) => {
    const scopeState = currentScopeState();
    const csrf = session.csrf;

    if (!scopeState || !csrf || entries.length === 0) {
      return;
    }
    if (!window.confirm(`Delete ${formatItemCount(entries.length)}?`)) {
      return;
    }

    await withBusy(async () => {
      const total = entries.length;
      let completed = 0;

      setTransfer({ active: true, label: `Deleting ${formatItemCount(total)}...`, progress: 0 });
      try {
        const results = await Promise.allSettled(
          entries.map(async (entry) => {
            try {
              return await deleteItem({ scope: scopeState.scope, tag: scopeState.tag, path: entry.path }, csrf);
            } finally {
              completed++;
              setTransfer({
                active: true,
                label: `Deleting ${completed} of ${total} item${total === 1 ? "" : "s"}...`,
                progress: Math.round((completed / total) * 100),
              });
            }
          }),
        );
        const successCount = results.filter((result) => result.status === "fulfilled").length;
        const failureCount = total - successCount;

        await refreshCurrentData();
        if (failureCount === 0) {
          setNotice(`Deleted ${formatItemCount(successCount)}.`);
          return;
        }
        if (successCount === 0) {
          setNotice(`Failed to delete ${formatItemCount(total)}.`);
          return;
        }

        setNotice(`Deleted ${successCount} of ${total} items. ${failureCount} failed.`);
      } finally {
        clearTransfer();
      }
    });
  };

  function getHeaderConfig() {
    if (showPlatformView && activePlatform) {
      return {
        destination: "library" as const,
        description: formatPlatformDescription(activePlatform),
        searchKey: "library" as const,
        searchPlaceholder: "Search platforms...",
        showPageHeader: true,
        showSearch: true,
        title: activePlatformDisplayName ?? activePlatform.name,
      };
    }
    if (showStatesView && activePlatform) {
      return {
        destination: "library" as const,
        description: "Download and remove grouped save-state bundles for the selected platform.",
        searchKey: "library" as const,
        searchPlaceholder: "Search platforms...",
        showPageHeader: false,
        showSearch: false,
        title: "Save States",
      };
    }
    if (showManagedBrowserView) {
      return {
        destination: "library" as const,
        description: "Browse, upload, rename, and delete managed content for the selected platform.",
        searchKey: "browser" as const,
        searchPlaceholder: "Search in current folder",
        showPageHeader: false,
        showSearch: false,
        title: browser.metadata?.title ?? "Browser",
      };
    }
    if (isFileBrowserTool(viewState)) {
      return {
        destination: "tools" as const,
        description: "Browse the device filesystem and manage folders safely.",
        searchKey: "file-browser" as const,
        searchPlaceholder: "Search in current folder",
        showPageHeader: false,
        showSearch: false,
        title: "File Browser",
      };
    }
    if (viewState.view === "tools" && viewState.tool === "logs") {
      return {
        destination: "tools" as const,
        description: "Scan, tail, and download device logs from .userdata.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: false,
        showSearch: false,
        title: "Log Viewer",
      };
    }
    if (viewState.view === "tools" && viewState.tool === "terminal") {
      return {
        destination: "tools" as const,
        description: "Open a PTY-backed shell when it is enabled on the handheld.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: false,
        showSearch: false,
        title: "Terminal",
      };
    }
    if (viewState.view === "tools" && viewState.tool === "collections") {
      return {
        destination: "tools" as const,
        description: "Edit collection files and their artwork assets.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: false,
        showSearch: false,
        title: "Collections",
      };
    }
    if (viewState.view === "tools" && viewState.tool === "screenshots") {
      return {
        destination: "tools" as const,
        description: "Preview, delete, and export screenshots from the device.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: false,
        showSearch: false,
        title: "Screenshots",
      };
    }
    if (viewState.view === "tools" && viewState.tool === "mac-dot-clean") {
      return {
        destination: "tools" as const,
        description: "Scan and remove safe macOS transfer artifacts from the SD card.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: false,
        showSearch: false,
        title: "Mac Dot Cleanup",
      };
    }
    if (viewState.view === "tools") {
      return {
        destination: "tools" as const,
        description: "Shortcuts and maintenance utilities for this device.",
        searchKey: "library" as const,
        searchPlaceholder: "Search",
        showPageHeader: true,
        showSearch: false,
        title: "Tools",
      };
    }

    return {
      destination: "library" as const,
      description: "Manage content by platform and jump into system-specific workspaces.",
      searchKey: "library" as const,
      searchPlaceholder: "Search platforms...",
      showPageHeader: true,
      showSearch: true,
      title: "Library",
    };
  }

  const header = getHeaderConfig();
  const shellSearchValue = searchByContext[header.searchKey];
  const content =
    showPlatformView && activePlatform ? (
      <PlatformView
        onBack={() => {
          navigate({ view: "dashboard", destination: "library" });
        }}
        onOpenResource={(resource: PlatformResource) => {
          if (resource === "states") {
            navigate({ view: "states", destination: "library", tag: activePlatform.tag });
            return;
          }

          navigate({ view: "browser", destination: "library", scope: resource, tag: activePlatform.tag });
        }}
        platform={activePlatform}
      />
    ) : showStatesView && activePlatform ? (
      <SaveStatesView
        csrf={session.csrf}
        onBack={() => {
          navigate({ view: "platform", destination: "library", tag: activePlatform.tag });
        }}
        onChanged={() => {
          void loadPlatforms(session.csrf);
        }}
        platform={activePlatform}
      />
    ) : (showManagedBrowserView || isFileBrowserTool(viewState)) && browser.metadata ? (
      <BrowserView
        busy={isBusy}
        canUploadFolder={canUploadFolder}
        hasMore={browser.hasMore}
        isLoadingMore={browser.isLoadingMore}
        notice={notice}
        onLoadMore={browser.loadMore}
        onBack={() => {
          if (isFileBrowserTool(viewState)) {
            navigate({ view: "tools", destination: "tools" });
            return;
          }

          navigate({ view: "platform", destination: "library", tag: viewState.tag });
        }}
        onCreateFolder={() => {
          void handleCreateFolder();
        }}
        onDeleteSelection={(entries) => {
          void handleDeleteSelection(entries);
        }}
        onDismissNotice={() => {
          setNotice(null);
        }}
        onEdit={(entry) => {
          openFileEditor(entry);
        }}
        onMoveSelection={(entries, destinationPath) => {
          void handleMoveSelection(entries, destinationPath);
        }}
        onNavigate={(path) => {
          if (isFileBrowserTool(viewState)) {
            navigate({ view: "tools", destination: "tools", tool: "file-browser", path });
            return;
          }

          navigate({
            view: "browser",
            destination: "library",
            scope: viewState.scope,
            tag: viewState.tag,
            path,
          });
        }}
        onRefresh={() => {
          void withBusy(async () => {
            await refreshCurrentData();
          });
        }}
        onRename={(entry) => {
          void handleRename(entry);
        }}
        onReplaceArt={(entry) => {
          void handleReplaceArt(entry);
        }}
        onSearchChange={(value) => {
          updateSearch(header.searchKey, value);
          if (isFileBrowserTool(viewState)) {
            setFileSearchResults(null);
          }
        }}
        onOpenSearchResult={(result) => {
          const parentPath =
            result.type === "directory"
              ? result.path
              : result.path.includes("/")
                ? result.path.slice(0, result.path.lastIndexOf("/"))
                : undefined;

          setFileSearchResults(null);
          navigate({ view: "tools", destination: "tools", tool: "file-browser", path: parentPath });
        }}
        onRunSearch={() => {
          void handleRunFileSearch();
        }}
        onUploadFolder={() => {
          void handleUploadFolder();
        }}
        onUploadZip={() => {
          void handleUploadZip();
        }}
        onUploadFiles={(selection) => {
          void handleUploadSelection(selection);
        }}
        csrf={session.csrf}
        response={browserResponse!}
        searchResults={isFileBrowserTool(viewState) ? fileSearchResults : null}
        scope={isFileBrowserTool(viewState) ? "files" : viewState.scope}
        search={shellSearchValue}
        tag={activePlatform?.tag}
        transfer={transfer}
      />
    ) : viewState.view === "browser" || isFileBrowserTool(viewState) ? (
      <div className="py-12 text-center text-sm text-[var(--muted)]">
        {browser.error ?? "Loading browser..."}
      </div>
    ) : viewState.view === "tools" && viewState.tool === "logs" ? (
      <LogsToolView
        csrf={session.csrf}
        initialPath={viewState.path}
        onBack={() => {
          navigate({ view: "tools", destination: "tools" });
        }}
        onPathChange={(path) => {
          navigate({ view: "tools", destination: "tools", tool: "logs", path });
        }}
      />
    ) : viewState.view === "tools" && viewState.tool === "terminal" ? (
      <TerminalToolView
        enabled={terminalEnabled}
        onBack={() => {
          navigate({ view: "tools", destination: "tools" });
        }}
        refreshSession={refreshSessionState}
      />
    ) : viewState.view === "tools" && viewState.tool === "collections" ? (
      <CollectionsToolView
        csrf={session.csrf}
        onBack={() => {
          navigate({ view: "tools", destination: "tools" });
        }}
      />
    ) : viewState.view === "tools" && viewState.tool === "screenshots" ? (
      <ScreenshotsToolView
        csrf={session.csrf}
        onBack={() => {
          navigate({ view: "tools", destination: "tools" });
        }}
      />
    ) : viewState.view === "tools" && viewState.tool === "mac-dot-clean" ? (
      <MacDotCleanToolView
        csrf={session.csrf}
        onBack={() => {
          navigate({ view: "tools", destination: "tools" });
        }}
      />
    ) : viewState.view === "tools" ? (
      <ToolsView
        onOpenCollections={() => {
          navigate({ view: "tools", destination: "tools", tool: "collections" });
        }}
        onOpenFileBrowser={() => {
          navigate({ view: "tools", destination: "tools", tool: "file-browser" });
        }}
        onOpenLogs={() => {
          navigate({ view: "tools", destination: "tools", tool: "logs" });
        }}
        onOpenMacDotClean={() => {
          navigate({ view: "tools", destination: "tools", tool: "mac-dot-clean" });
        }}
        onOpenScreenshots={() => {
          navigate({ view: "tools", destination: "tools", tool: "screenshots" });
        }}
        onOpenTerminal={() => {
          navigate({ view: "tools", destination: "tools", tool: "terminal" });
        }}
        terminalEnabled={terminalEnabled}
      />
    ) : (
      <DashboardShell
        emuFilter={libraryEmuFilter}
        groups={visiblePlatformGroups}
        onChangeEmuFilter={updateLibraryEmuFilter}
        onSelectPlatform={(tag) => {
          navigate({ view: "platform", destination: "library", tag });
        }}
        showEmptyPlatforms={showEmptyPlatforms}
        onToggleShowEmpty={updateShowEmpty}
      />
    );

  return (
    <>
      <AppShell
        description={header.description}
        destination={header.destination}
        fillViewport={viewState.view === "tools" && viewState.tool === "terminal"}
        onDestinationChange={navigateToDestination}
        onDisconnect={handleDisconnect}
        onSearchChange={(value) => {
          updateSearch(header.searchKey, value);
        }}
        searchPlaceholder={header.searchPlaceholder}
        searchValue={shellSearchValue}
        showPageHeader={header.showPageHeader}
        showSearch={header.showSearch}
        title={header.title}
        transfer={transfer}
      >
        {content}
        {editor ? (
          <FileEditorModal
            entry={editor.entry}
            initialContent={editor.content}
            loadError={editor.loadError}
            loading={editor.loading}
            onCancel={() => {
              if (!editor.saving) {
                setEditor(null);
              }
            }}
            onSave={(nextContent) => {
              void handleSaveEditor(nextContent);
            }}
            saving={editor.saving}
          />
        ) : null}
        {zipExtractDialog ? (
          <ZipExtractDialog
            checking={zipExtractDialog.checking}
            conflicts={zipExtractDialog.preflight}
            overwriteExisting={zipExtractDialog.overwriteExisting}
            preview={zipExtractDialog.preview}
            onCancel={() => {
              clearZipPreviewAbort();
              setZipExtractDialog(null);
            }}
            onConfirm={(options) => {
              void handleConfirmZipExtract(options);
            }}
            onOverwriteChange={(overwriteExisting) => {
              setZipExtractDialog((current) =>
                current
                  ? {
                      ...current,
                      overwriteExisting,
                      preflight: null,
                    }
                  : current,
              );
            }}
            onStrategyChange={(strategy) => {
              setZipExtractDialog((current) =>
                current
                  ? {
                      ...current,
                      strategy,
                      preflight: null,
                    }
                  : current,
              );
            }}
            strategy={zipExtractDialog.strategy}
          />
        ) : null}
      </AppShell>
      {connectionLost ? (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 px-4 backdrop-blur-sm">
          <section className="w-full max-w-md rounded-[28px] border border-[var(--border)] bg-[var(--panel)] px-6 py-6 text-[var(--text)] shadow-[var(--shadow)]">
            <p className="text-xs font-semibold uppercase tracking-[0.2em] text-rose-300">Connection lost</p>
            <h2 className="mt-3 text-2xl font-bold">The handheld app is unavailable.</h2>
            <p className="mt-3 text-sm text-[var(--muted)]">
              Work is paused until the device app reconnects. This page keeps retrying automatically every 2 seconds.
            </p>
            <p className="mt-3 text-sm text-[var(--muted)]">
              Keep this tab open. Your current workspace will resume in place as soon as the connection returns.
            </p>
          </section>
        </div>
      ) : null}
    </>
  );
}
