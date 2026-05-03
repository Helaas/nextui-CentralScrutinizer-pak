import { useEffect, useRef, useState } from "react";
import JSZip from "jszip";

import { buildDownloadUrl, getBrowserAll } from "../lib/api";
import { BROWSER_MOVE_DRAG_TYPE } from "../lib/drag-types";
import type {
  BrowserEntry,
  BrowserResponse,
  BrowserScope,
  FileSearchResult,
  TransferState,
} from "../lib/types";
import { Breadcrumbs } from "./breadcrumbs";
import { BrowserFilesToolbar } from "./browser-files-toolbar";
import { BrowserWorkspaceCard } from "./browser-workspace-card";
import { BrowserTable } from "./browser-table";
import { DropZone } from "./drop-zone";
import { NoticeToast } from "./notice-toast";
import { TransferBar } from "./transfer-bar";

const BROWSER_MOVE_IGNORED_DRAG_TYPES = [BROWSER_MOVE_DRAG_TYPE];

function getDisplayRoot(scope: BrowserScope, response: BrowserResponse): string {
  return scope === "files" ? "SD Card" : response.rootPath;
}

function getWorkspaceTitle(scope: BrowserScope, response: BrowserResponse): string {
  const source = response.path || getDisplayRoot(scope, response) || response.title;
  const parts = source.split("/").filter(Boolean);

  return parts[parts.length - 1] ?? source;
}

function getFullPath(scope: BrowserScope, response: BrowserResponse): string {
  const root = getDisplayRoot(scope, response);

  return response.path ? `${root}/${response.path}` : root;
}

function formatItemCount(count: number): string {
  return `${count} item${count === 1 ? "" : "s"}`;
}

function isPreviewableImage(name: string): boolean {
  return /\.(png|jpe?g|bmp|gif|webp|svg)$/i.test(name);
}

function normalizeBrowserPath(path?: string | null): string | undefined {
  const trimmed = path?.trim();

  return trimmed ? trimmed : undefined;
}

function getParentBrowserPath(path?: string): string | undefined {
  if (!path) {
    return undefined;
  }

  const lastSlash = path.lastIndexOf("/");

  return lastSlash >= 0 ? path.slice(0, lastSlash) : undefined;
}

function canMoveEntriesToDestination(entries: BrowserEntry[], destinationPath?: string): boolean {
  const normalizedDestination = normalizeBrowserPath(destinationPath);

  return entries.some((entry) => {
    const nextPath = normalizedDestination ? `${normalizedDestination}/${entry.name}` : entry.name;

    if (entry.type === "directory" && normalizedDestination) {
      if (normalizedDestination === entry.path || normalizedDestination.startsWith(`${entry.path}/`)) {
        return false;
      }
    }

    return nextPath !== entry.path;
  });
}

function BrowserMoveModal({
  csrf,
  entries,
  initialResponse,
  onCancel,
  onConfirm,
}: {
  csrf?: string | null;
  entries: BrowserEntry[];
  initialResponse: BrowserResponse;
  onCancel: () => void;
  onConfirm: (destinationPath: string) => void;
}) {
  const initialPath = getParentBrowserPath(normalizeBrowserPath(initialResponse.path));
  const initialResponsePath = normalizeBrowserPath(initialResponse.path);
  const [currentPath, setCurrentPath] = useState<string | undefined>(initialPath);
  const [currentResponse, setCurrentResponse] = useState<BrowserResponse | null>(
    initialResponsePath === initialPath ? initialResponse : null,
  );
  const [loading, setLoading] = useState(initialResponsePath !== initialPath);
  const [loadError, setLoadError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;

    if (initialResponsePath === currentPath) {
      setCurrentResponse(initialResponse);
      setLoadError(null);
      setLoading(false);
      return () => {
        active = false;
      };
    }
    if (!csrf) {
      setCurrentResponse(null);
      setLoadError("Missing session csrf token.");
      setLoading(false);
      return () => {
        active = false;
      };
    }

    setLoading(true);
    setLoadError(null);
    void getBrowserAll("files", csrf, undefined, currentPath)
      .then((nextResponse) => {
        if (!active) {
          return;
        }

        setCurrentResponse(nextResponse);
      })
      .catch((error) => {
        if (!active) {
          return;
        }

        setCurrentResponse(null);
        setLoadError(error instanceof Error ? error.message : "Could not load folders.");
      })
      .finally(() => {
        if (active) {
          setLoading(false);
        }
      });

    return () => {
      active = false;
    };
  }, [csrf, currentPath, initialResponse, initialResponsePath]);

  const directories = currentResponse?.entries.filter((entry) => entry.type === "directory") ?? [];
  const canMoveHere = currentResponse ? canMoveEntriesToDestination(entries, currentResponse.path) : false;
  const currentLabel = currentResponse?.path ? `SD Card/${currentResponse.path}` : "SD Card";

  return (
    <div
      aria-labelledby="move-items-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      role="dialog"
    >
      <div className="flex h-full max-h-[82vh] w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-xl">
        <div className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <div className="min-w-0">
            <h2 className="truncate text-base font-semibold text-[var(--text)]" id="move-items-title">
              Move {formatItemCount(entries.length)}
            </h2>
            <p className="text-sm text-[var(--muted)]">Browse folders and choose a destination visually.</p>
          </div>
          <button
            aria-label="Close move picker"
            className="rounded-md px-2 py-1 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onCancel}
            type="button"
          >
            ✕
          </button>
        </div>

        <div className="flex-1 space-y-4 overflow-auto px-5 py-4">
          <div className="space-y-3 rounded-xl border border-[var(--border)] bg-black/10 px-4 py-4">
            <div>
              <p className="text-xs font-semibold uppercase tracking-[0.15em] text-[var(--muted)]">Destination</p>
              <p className="mt-1 text-sm text-[var(--text)]">{currentLabel}</p>
            </div>
            {currentResponse ? (
              <Breadcrumbs
                ariaLabel="Move destination path"
                items={currentResponse.breadcrumbs}
                onSelect={(path) => {
                  setCurrentPath(normalizeBrowserPath(path));
                }}
                rootLabel="SD Card"
              />
            ) : null}
          </div>

          {loadError ? (
            <div className="rounded-xl border border-rose-300/25 bg-rose-500/10 px-4 py-3 text-sm text-rose-100">
              {loadError}
            </div>
          ) : null}

          <section className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)]">
            <div className="border-b border-[var(--line)] px-4 py-3">
              <p className="text-sm text-[var(--muted)]">Open a folder below, then choose “Move Here”.</p>
            </div>
            {loading ? (
              <div className="px-5 py-10 text-center text-sm italic text-[var(--muted)]">Loading folders...</div>
            ) : directories.length === 0 ? (
              <div className="px-5 py-10 text-center text-sm italic text-[var(--muted)]">No folders in this location.</div>
            ) : (
              <div className="divide-y divide-[var(--line)]">
                {directories.map((entry) => (
                  <button
                    key={entry.path}
                    aria-label={`Open folder ${entry.name}`}
                    className="flex w-full items-center justify-between gap-3 px-4 py-3 text-left text-sm transition hover:bg-white/[0.03]"
                    onClick={() => {
                      setCurrentPath(normalizeBrowserPath(entry.path));
                    }}
                    type="button"
                  >
                    <div className="min-w-0">
                      <p className="truncate font-medium text-[var(--text)]">{entry.name}</p>
                      <p className="truncate text-xs text-[var(--muted)]">{entry.path}</p>
                    </div>
                    <span className="shrink-0 text-xs font-semibold uppercase tracking-[0.15em] text-[var(--muted)]">
                      Open
                    </span>
                  </button>
                ))}
              </div>
            )}
          </section>
        </div>

        <div className="flex flex-col gap-2 border-t border-[var(--line)] px-5 py-4 sm:flex-row sm:items-center sm:justify-between">
          <p className="text-xs text-[var(--muted)]">
            {canMoveHere ? "Ready to move into the selected folder." : "Selected items are already in this folder."}
          </p>
          <div className="flex gap-2 sm:justify-end">
            <button
              className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={onCancel}
              type="button"
            >
              Cancel
            </button>
            <button
              className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
              disabled={!currentResponse || loading || !canMoveHere}
              onClick={() => {
                if (currentResponse) {
                  onConfirm(currentResponse.path);
                }
              }}
              type="button"
            >
              Move Here
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}

export function BrowserView({
  busy = false,
  canUploadFolder = false,
  csrf,
  hasMore = false,
  isLoadingMore = false,
  notice,
  onLoadMore,
  response,
  scope,
  search = "",
  tag,
  onBack,
  onCreateFolder,
  onDeleteSelection,
  onDismissNotice,
  onEdit,
  onMoveSelection,
  onNavigate,
  onOpenSearchResult,
  onRunSearch,
  onRefresh,
  onRename,
  onReplaceArt,
  onSearchChange,
  onUploadFolder,
  onUploadFiles,
  searchResults,
  transfer,
}: {
  busy?: boolean;
  canUploadFolder?: boolean;
  csrf?: string | null;
  hasMore?: boolean;
  isLoadingMore?: boolean;
  notice?: string | null;
  onLoadMore?: () => void;
  response: BrowserResponse;
  scope: BrowserScope;
  search?: string;
  tag?: string;
  onBack: () => void;
  onCreateFolder: () => void;
  onDeleteSelection: (entries: BrowserEntry[]) => void;
  onDismissNotice?: () => void;
  onEdit?: (entry: BrowserEntry) => void;
  onMoveSelection?: (entries: BrowserEntry[], destinationPath: string) => void;
  onNavigate: (path?: string) => void;
  onOpenSearchResult?: (result: FileSearchResult) => void;
  onRunSearch?: () => void;
  onRefresh: () => void;
  onRename: (entry: BrowserEntry) => void;
  onReplaceArt: (entry: BrowserEntry) => void;
  onSearchChange: (value: string) => void;
  onUploadFolder?: () => void;
  onUploadFiles: (files: File[]) => void;
  searchResults?: FileSearchResult[] | null;
  transfer: TransferState;
}) {
  const uploadInputRef = useRef<HTMLInputElement | null>(null);
  const [previewPath, setPreviewPath] = useState<string | null>(null);
  const [selectedPaths, setSelectedPaths] = useState<string[]>([]);
  const [bulkDownloadBusy, setBulkDownloadBusy] = useState(false);
  const [localNotice, setLocalNotice] = useState<string | null>(null);
  const [moveSelectionEntries, setMoveSelectionEntries] = useState<BrowserEntry[] | null>(null);
  const isFiles = scope === "files";
  const allowDroppedDirectories = canUploadFolder && (isFiles || scope === "roms");
  const fullPath = getFullPath(scope, response);
  const responseTotalCount = Number.isFinite(response.totalCount) ? response.totalCount : 0;
  const totalCount = Math.max(responseTotalCount, response.entries.length);
  const itemCountLabel = formatItemCount(totalCount);
  const entries = response.entries;
  const remaining = Math.max(totalCount - entries.length, 0);
  const selectedEntries = entries.filter((entry) => selectedPaths.includes(entry.path));
  const allSelected = entries.length > 0 && entries.every((entry) => selectedPaths.includes(entry.path));
  const partiallySelected = selectedEntries.length > 0 && !allSelected;
  const hasDirectorySelection = selectedEntries.some((entry) => entry.type === "directory");
  const canDownloadSelection = Boolean(csrf) && selectedEntries.length > 0 && !hasDirectorySelection;
  const visibleNotice = localNotice ?? notice;

  useEffect(() => {
    const visiblePaths = new Set(entries.map((entry) => entry.path));

    setSelectedPaths((current) => {
      const next = current.filter((path) => visiblePaths.has(path));

      return next.length === current.length && next.every((path, index) => path === current[index])
        ? current
        : next;
    });
  }, [entries]);

  useEffect(() => {
    if (searchResults) {
      setSelectedPaths([]);
    }
  }, [searchResults]);

  useEffect(() => {
    if (notice) {
      setLocalNotice(null);
    }
  }, [notice]);

  function dismissVisibleNotice() {
    if (localNotice) {
      setLocalNotice(null);
      return;
    }

    onDismissNotice?.();
  }

  async function handleDownloadSelection() {
    if (!csrf || selectedEntries.length === 0 || hasDirectorySelection) {
      if (hasDirectorySelection) {
        setLocalNotice("Bulk download works with file-only selections.");
      }
      return;
    }

    setBulkDownloadBusy(true);
    setLocalNotice(null);
    try {
      const zip = new JSZip();

      for (const entry of selectedEntries) {
        const downloadResponse = await fetch(buildDownloadUrl("files", entry.path, tag, csrf));

        if (!downloadResponse.ok) {
          throw new Error(`Could not download ${entry.name}`);
        }
        zip.file(entry.name, await downloadResponse.arrayBuffer());
      }

      const blob = await zip.generateAsync({ type: "blob" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");

      link.href = url;
      link.download = `central-scrutinizer-files-${new Date().toISOString().replace(/[:.]/g, "-")}.zip`;
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      setLocalNotice(error instanceof Error ? error.message : "Download failed.");
    } finally {
      setBulkDownloadBusy(false);
    }
  }

  return (
    <DropZone
      allowDirectories={allowDroppedDirectories}
      disabled={transfer.active}
      ignoredDragTypes={BROWSER_MOVE_IGNORED_DRAG_TYPES}
      onDrop={onUploadFiles}
    >
      <div className="space-y-5">
      {isFiles ? (
        <>
          <button
            className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onBack}
            type="button"
          >
            <span aria-hidden="true">←</span>
            Back
          </button>
          <BrowserFilesToolbar
            canRunSearch={search.trim().length > 0}
            busy={busy}
            canUploadFolder={canUploadFolder}
            onClearSearch={
              searchResults
                ? () => {
                    onSearchChange("");
                  }
                : undefined
            }
            onCreateFolder={onCreateFolder}
            onNavigate={onNavigate}
            onRefresh={onRefresh}
            onRunSearch={onRunSearch}
            onSearchChange={onSearchChange}
            onUploadFolder={onUploadFolder}
            onUploadFile={() => {
              uploadInputRef.current?.click();
            }}
            response={response}
            searchResultsActive={Boolean(searchResults)}
            search={search}
          />
        </>
      ) : (
        <BrowserWorkspaceCard
          breadcrumbs={response.breadcrumbs}
          busy={busy}
          canUploadFolder={canUploadFolder}
          itemCount={totalCount}
          onBack={onBack}
          onCreateFolder={onCreateFolder}
          onNavigate={onNavigate}
          onRefresh={onRefresh}
          onSearchChange={onSearchChange}
          onUploadFolder={onUploadFolder}
          onUploadFile={() => {
            uploadInputRef.current?.click();
          }}
          rootLabel={getDisplayRoot(scope, response)}
          scope={scope}
          search={search}
          title={getWorkspaceTitle(scope, response)}
        />
      )}
      {isFiles && !searchResults && selectedEntries.length > 0 ? (
        <section className="rounded-[20px] border border-[var(--border)] bg-[var(--panel)] px-4 py-4">
          <div className="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
            <div>
              <h3 className="font-semibold">{formatItemCount(selectedEntries.length)} selected</h3>
              <p className="text-sm text-[var(--muted)]">
                {hasDirectorySelection
                  ? "Bulk download works with file-only selections. Move and delete still work for folders."
                  : "Move, delete, or download the selected files from this folder."}
              </p>
            </div>
            <div className="flex flex-wrap gap-2">
              <button
                className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
                disabled={!canDownloadSelection || bulkDownloadBusy}
                onClick={() => {
                  void handleDownloadSelection();
                }}
                type="button"
              >
                {bulkDownloadBusy ? "Creating Zip..." : "Download Selected"}
              </button>
              {onMoveSelection ? (
                <button
                  className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                  disabled={busy || bulkDownloadBusy}
                  onClick={() => {
                    setMoveSelectionEntries(selectedEntries);
                  }}
                  type="button"
                >
                  Move Selected
                </button>
              ) : null}
              <button
                className="rounded-md border border-rose-300/30 px-4 py-2 text-sm font-medium text-rose-100 transition hover:border-rose-200/40 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy || bulkDownloadBusy}
                onClick={() => {
                  onDeleteSelection(selectedEntries);
                }}
                type="button"
              >
                Delete Selected
              </button>
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy || bulkDownloadBusy}
                onClick={() => {
                  setSelectedPaths([]);
                }}
                type="button"
              >
                Clear Selection
              </button>
            </div>
          </div>
        </section>
      ) : null}
      <TransferBar
        active={transfer.active}
        cancellable={transfer.cancellable}
        label={transfer.label}
        onCancel={transfer.onCancel}
        progress={transfer.progress}
      />
      {response.truncated ? (
        <section className="rounded-lg border border-amber-300/25 bg-amber-950/20 px-4 py-3 text-sm text-amber-100">
          This folder contains more entries than the browser will list at once. Only the first {totalCount.toLocaleString()}{" "}
          entries are reachable here — split the folder into subfolders to see the rest.
        </section>
      ) : null}
      {visibleNotice ? <NoticeToast message={visibleNotice} onDismiss={dismissVisibleNotice} /> : null}
      {isFiles && searchResults ? (
        <section className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-4">
          <div className="mb-4 flex items-center justify-between gap-3">
            <div>
              <h3 className="font-semibold">Search Results</h3>
              <p className="text-sm text-[var(--muted)]">{searchResults.length} matches</p>
            </div>
          </div>
          {searchResults.length === 0 ? (
            <p className="text-sm text-[var(--muted)]">No matches found.</p>
          ) : (
            <div className="space-y-2">
              {searchResults.map((result) => (
                <button
                  key={result.path}
                  className="flex w-full items-center justify-between rounded-xl border border-[var(--border)] px-4 py-3 text-left text-sm transition hover:border-[var(--accent)]/40"
                  onClick={() => {
                    onOpenSearchResult?.(result);
                  }}
                  type="button"
                >
                  <span className="truncate">{result.path}</span>
                  <span className="ml-3 shrink-0 text-xs uppercase tracking-[0.15em] text-[var(--muted)]">
                    {result.type}
                  </span>
                </button>
              ))}
            </div>
          )}
        </section>
      ) : (
        <BrowserTable
          allSelected={allSelected}
          busy={busy}
          csrf={csrf}
          entries={entries}
          onDelete={(entry) => {
            onDeleteSelection([entry]);
          }}
          onEdit={isFiles ? onEdit : undefined}
          onMoveEntries={isFiles ? onMoveSelection : undefined}
          onNavigate={onNavigate}
          onNavigateParent={
            isFiles && response.breadcrumbs.length > 0
              ? () => {
                  const parent = response.breadcrumbs[response.breadcrumbs.length - 2];
                  onNavigate(parent?.path);
                }
              : undefined
          }
          onRename={onRename}
          onReplaceArt={isFiles ? undefined : onReplaceArt}
          onSelectAll={
            isFiles
              ? (checked) => {
                  setSelectedPaths(checked ? entries.map((entry) => entry.path) : []);
                }
              : undefined
          }
          onSelectEntry={
            isFiles
              ? (entry, checked) => {
                  setSelectedPaths((current) => {
                    if (checked) {
                      return current.includes(entry.path) ? current : [...current, entry.path];
                    }

                    return current.filter((path) => path !== entry.path);
                  });
                }
              : undefined
          }
          selectedPaths={isFiles ? selectedPaths : undefined}
          someSelected={partiallySelected}
          scope={scope}
          tag={tag}
        />
      )}
      {!searchResults && hasMore && onLoadMore ? (
        <div className="flex justify-center">
          <button
            className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-5 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-60"
            disabled={isLoadingMore}
            onClick={onLoadMore}
            type="button"
          >
            {isLoadingMore ? "Loading..." : `Load more (${remaining.toLocaleString()} remaining)`}
          </button>
        </div>
      ) : null}
      {isFiles ? (
        <div className="flex flex-wrap gap-2">
          {entries
            .filter((entry) => entry.type !== "directory" && isPreviewableImage(entry.name))
            .slice(0, 8)
            .map((entry) => (
              <button
                key={entry.path}
                className="rounded-md border border-[var(--border)] px-3 py-2 text-xs"
                onClick={() => {
                  setPreviewPath(buildDownloadUrl("files", entry.path, tag, csrf));
                }}
                type="button"
              >
                Preview {entry.name}
              </button>
            ))}
        </div>
      ) : null}
      {isFiles ? (
        <footer className="rounded-lg border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          <div className="flex flex-col gap-1">
            <p>{itemCountLabel}</p>
            <p className="break-all">{fullPath}</p>
          </div>
        </footer>
      ) : null}
      <input
        ref={uploadInputRef}
        className="hidden"
        multiple
        onChange={(event) => {
          const files = Array.from(event.target.files ?? []);
          if (files.length > 0) {
            onUploadFiles(files);
          }
          event.target.value = "";
        }}
        type="file"
      />
      {previewPath ? (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
          <div className="max-h-[90vh] max-w-6xl overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)]">
            <div className="flex justify-end p-3">
              <button className="rounded-md px-3 py-2 text-sm text-[var(--muted)]" onClick={() => setPreviewPath(null)} type="button">
                Close
              </button>
            </div>
            <img alt="Preview" className="max-h-[80vh] w-full object-contain" src={previewPath} />
          </div>
        </div>
      ) : null}
      {moveSelectionEntries ? (
        <BrowserMoveModal
          csrf={csrf}
          entries={moveSelectionEntries}
          initialResponse={response}
          onCancel={() => {
            setMoveSelectionEntries(null);
          }}
          onConfirm={(destinationPath) => {
            setMoveSelectionEntries(null);
            onMoveSelection?.(moveSelectionEntries, destinationPath);
          }}
        />
      ) : null}
    </div>
    </DropZone>
  );
}
