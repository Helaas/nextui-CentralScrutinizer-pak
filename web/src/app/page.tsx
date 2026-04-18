"use client";

import { useEffect, useState } from "react";

import { AppShell } from "../components/app-shell";
import { BrowserView } from "../components/browser-view";
import { DashboardShell } from "../components/dashboard-shell";
import { FileEditorModal } from "../components/file-editor-modal";
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
  beginUploadFilesBatched,
  createFolder,
  deleteItem,
  getBrowser,
  getPlatforms,
  getSession,
  pairBrowser,
  pairBrowserQr,
  readTextFile,
  replaceArt,
  renameItem,
  revokeBrowser,
  searchFiles,
  writeTextFile,
} from "../lib/api";
import { getBrowserId } from "../lib/browser-id";
import {
  createPlatformDisplayNames,
  filterPlatformGroups,
  flattenPlatformGroups,
} from "../lib/platform-display";
import {
  getDestination,
  readShowEmptyPlatforms,
  readViewState,
  type AppViewState,
  writeViewState,
} from "../lib/navigation";
import { PLAINTEXT_MAX_BYTES } from "../lib/plaintext";
import type {
  BrowserEntry,
  BrowserResponse,
  BrowserScope,
  FileSearchResult,
  PlatformGroup,
  PlatformSummary,
  SessionResponse,
  TransferState,
} from "../lib/types";

type DirectoryCapableInput = HTMLInputElement & { webkitdirectory?: boolean };

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

async function pickFolderFiles(): Promise<File[]> {
  return new Promise((resolve) => {
    const input = document.createElement("input") as DirectoryCapableInput;

    input.type = "file";
    input.multiple = true;
    input.webkitdirectory = true;
    input.addEventListener("change", () => {
      resolve(Array.from(input.files ?? []));
    });
    input.click();
  });
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
  const [platformGroups, setPlatformGroups] = useState<PlatformGroup[]>([]);
  const [browserResponse, setBrowserResponse] = useState<BrowserResponse | null>(null);
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

  function navigate(next: AppViewState, replace = false) {
    setViewState(next);
    setNotice(null);
    if (typeof window !== "undefined") {
      const url = writeViewState(next, { showEmptyPlatforms });

      if (replace) {
        window.history.replaceState(null, "", url);
      } else {
        window.history.pushState(null, "", url);
      }
    }
  }

  async function loadPlatforms(currentCsrf = session?.csrf) {
    if (!currentCsrf) {
      return;
    }

    const response = await getPlatforms(currentCsrf);

    setPlatformGroups(response.groups);
  }

  async function refreshSessionState(): Promise<SessionResponse> {
    const nextSession = normalizeSession(await getSession());

    setSession(nextSession);
    setConnectionLost(false);
    setRetryUnavailableSession(false);
    return nextSession;
  }

  async function refreshBrowser(currentView = viewState, currentCsrf = session?.csrf) {
    if (!currentCsrf) {
      setBrowserResponse(null);
      return;
    }

    if (currentView.view === "browser") {
      setBrowserResponse(await getBrowser(currentView.scope, currentCsrf, currentView.tag, currentView.path));
      return;
    }
    if (isFileBrowserTool(currentView)) {
      setBrowserResponse(await getBrowser("files", currentCsrf, undefined, currentView.path));
      return;
    }

    setBrowserResponse(null);
  }

  async function refreshCurrentData(currentView = viewState, currentCsrf = session?.csrf) {
    await loadPlatforms(currentCsrf);
    await refreshBrowser(currentView, currentCsrf);
  }

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
            await refreshCurrentData({ view: "dashboard", destination: "library" }, pairedSession.csrf);
            return;
          } catch (error) {
            if (!active) {
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
          navigate({ view: "pair" }, true);
          return;
        }

        if (viewState.view === "pair") {
          navigate({ view: "dashboard", destination: "library" }, true);
          await refreshCurrentData({ view: "dashboard", destination: "library" }, nextSession.csrf);
          return;
        }

        await refreshCurrentData(viewState, nextSession.csrf);
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
    if (!session?.paired || connectionLost) {
      return;
    }

    void refreshBrowser();
  }, [connectionLost, session?.paired, viewState]);

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
          setPairMessage(retryUnavailableSession ? null : getReconnectMessage());
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
          await refreshCurrentData({ view: "dashboard", destination: "library" }, nextSession.csrf);
          return;
        }
        if (connectionLost || retryUnavailableSession) {
          setConnectionLost(false);
          setRetryUnavailableSession(false);
          await refreshCurrentData(viewState, nextSession.csrf);
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
            await refreshCurrentData({ view: "dashboard", destination: "library" }, pairedSession.csrf);
          } catch (error) {
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
    showEmptyPlatforms,
  );
  const visiblePlatformDisplayNames = createPlatformDisplayNames(
    flattenPlatformGroups(visiblePlatformGroups),
  );
  const activePlatformDisplayName = activePlatform
    ? visiblePlatformDisplayNames.get(activePlatform.tag) ?? activePlatform.name
    : undefined;
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
      window.history.replaceState(null, "", writeViewState(viewState, { showEmptyPlatforms: value }));
    }
  }

  const handleUploadFiles = async (files: File[]) => {
    const scopeState = currentScopeState();
    const csrf = session.csrf;

    if (!scopeState || !csrf || files.length === 0) {
      return;
    }

    await withBusy(async () => {
      const label = `Uploading ${files.length} file${files.length === 1 ? "" : "s"}`;
      const upload = beginUploadFilesBatched({ ...scopeState, files }, csrf, (progress) => {
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

        const plural = (n: number) => (n === 1 ? "" : "s");

        if (summary.cancelled && summary.uploaded === 0) {
          setNotice("Upload cancelled.");
        } else if (summary.cancelled) {
          setNotice(`Upload cancelled after ${summary.uploaded} of ${files.length} file${plural(files.length)}.`);
        } else if (summary.failed > 0 && summary.uploaded === 0) {
          setNotice("Upload failed.");
        } else if (summary.failed > 0) {
          setNotice(`Uploaded ${summary.uploaded} file${plural(summary.uploaded)}, ${summary.failed} failed.`);
        } else {
          setNotice(`Uploaded ${summary.uploaded} file${plural(summary.uploaded)}.`);
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

    const files = await pickFolderFiles();

    if (files.length > 0) {
      await handleUploadFiles(files);
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

    if (!scopeState || !csrf || !nextName || nextName === entry.name) {
      return;
    }

    await withBusy(async () => {
      await renameItem(
        {
          scope: scopeState.scope,
          tag: scopeState.tag,
          from: entry.path,
          to: buildRenamedPath(entry.path, nextName.trim()),
        },
        csrf,
      );
      await refreshCurrentData();
      setNotice(`Renamed ${entry.name} to ${nextName.trim()}.`);
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
      const results = await Promise.allSettled(
        entries.map(async (entry) => deleteItem({ scope: scopeState.scope, tag: scopeState.tag, path: entry.path }, csrf)),
      );
      const successCount = results.filter((result) => result.status === "fulfilled").length;
      const failureCount = entries.length - successCount;

      await refreshCurrentData();
      if (failureCount === 0) {
        setNotice(`Deleted ${formatItemCount(successCount)}.`);
        return;
      }
      if (successCount === 0) {
        setNotice(`Failed to delete ${formatItemCount(entries.length)}.`);
        return;
      }

      setNotice(`Deleted ${successCount} of ${entries.length} items. ${failureCount} failed.`);
    });
  };

  function getHeaderConfig() {
    if (viewState.view === "platform" && activePlatform) {
      return {
        destination: "library" as const,
        description: `${activePlatform.counts.roms} ROMs, ${activePlatform.counts.saves} saves, ${activePlatform.counts.states} states, ${activePlatform.counts.bios} BIOS files, ${activePlatform.counts.overlays} overlays, ${activePlatform.counts.cheats} cheats.`,
        searchKey: "library" as const,
        searchPlaceholder: "Search platforms...",
        showPageHeader: true,
        showSearch: true,
        title: activePlatformDisplayName ?? activePlatform.name,
      };
    }
    if (viewState.view === "states" && activePlatform) {
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
    if (viewState.view === "browser") {
      return {
        destination: "library" as const,
        description: "Browse, upload, rename, and delete managed content for the selected platform.",
        searchKey: "browser" as const,
        searchPlaceholder: "Search in current folder",
        showPageHeader: false,
        showSearch: false,
        title: browserResponse?.title ?? "Browser",
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
    viewState.view === "platform" && activePlatform ? (
      <PlatformView
        onBack={() => {
          navigate({ view: "dashboard", destination: "library" });
        }}
        onOpenResource={(resource) => {
          if (resource === "states") {
            navigate({ view: "states", destination: "library", tag: activePlatform.tag });
            return;
          }

          navigate({ view: "browser", destination: "library", scope: resource, tag: activePlatform.tag });
        }}
        platform={activePlatform}
      />
    ) : viewState.view === "states" && activePlatform ? (
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
    ) : (viewState.view === "browser" || isFileBrowserTool(viewState)) && browserResponse ? (
      <BrowserView
        busy={isBusy}
        canUploadFolder={canUploadFolder}
        notice={notice}
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
        onEdit={(entry) => {
          openFileEditor(entry);
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
        onUploadFiles={(files) => {
          void handleUploadFiles(files);
        }}
        csrf={session.csrf}
        response={browserResponse}
        searchResults={isFileBrowserTool(viewState) ? fileSearchResults : null}
        scope={isFileBrowserTool(viewState) ? "files" : viewState.scope}
        search={shellSearchValue}
        tag={activePlatform?.tag}
        transfer={transfer}
      />
    ) : viewState.view === "browser" || isFileBrowserTool(viewState) ? (
      <div className="py-12 text-center text-sm text-[var(--muted)]">Loading browser...</div>
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
      <CollectionsToolView csrf={session.csrf} />
    ) : viewState.view === "tools" && viewState.tool === "screenshots" ? (
      <ScreenshotsToolView csrf={session.csrf} />
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
        groups={visiblePlatformGroups}
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
