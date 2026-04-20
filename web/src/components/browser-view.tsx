import { useRef, useState } from "react";

import { buildDownloadUrl } from "../lib/api";
import type {
  BrowserEntry,
  BrowserResponse,
  BrowserScope,
  FileSearchResult,
  TransferState,
} from "../lib/types";
import { BrowserFilesToolbar } from "./browser-files-toolbar";
import { BrowserWorkspaceCard } from "./browser-workspace-card";
import { BrowserTable } from "./browser-table";
import { DropZone } from "./drop-zone";
import { TransferBar } from "./transfer-bar";

function filterEntries(entries: BrowserEntry[], search: string): BrowserEntry[] {
  const query = search.trim().toLowerCase();

  if (!query) {
    return entries;
  }

  return entries.filter(
    (entry) => entry.name.toLowerCase().includes(query) || entry.path.toLowerCase().includes(query),
  );
}

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

export function BrowserView({
  busy = false,
  canUploadFolder = false,
  csrf,
  notice,
  response,
  scope,
  search = "",
  tag,
  onBack,
  onCreateFolder,
  onDeleteSelection,
  onEdit,
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
  notice?: string | null;
  response: BrowserResponse;
  scope: BrowserScope;
  search?: string;
  tag?: string;
  onBack: () => void;
  onCreateFolder: () => void;
  onDeleteSelection: (entries: BrowserEntry[]) => void;
  onEdit?: (entry: BrowserEntry) => void;
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
  const isFiles = scope === "files";
  const allowDroppedDirectories = canUploadFolder && (isFiles || scope === "roms");
  const fullPath = getFullPath(scope, response);
  const itemCount = response.entries.length;
  const entries = filterEntries(response.entries, search);

  return (
    <DropZone allowDirectories={allowDroppedDirectories} disabled={transfer.active} onDrop={onUploadFiles}>
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
          itemCount={itemCount}
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
      <TransferBar
        active={transfer.active}
        cancellable={transfer.cancellable}
        label={transfer.label}
        onCancel={transfer.onCancel}
        progress={transfer.progress}
      />
      {notice ? (
        <div className="rounded-lg border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}
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
          busy={busy}
          csrf={csrf}
          entries={entries}
          onDelete={(entry) => {
            onDeleteSelection([entry]);
          }}
          onEdit={isFiles ? onEdit : undefined}
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
          scope={scope}
          tag={tag}
        />
      )}
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
            <p>{formatItemCount(itemCount)}</p>
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
    </div>
    </DropZone>
  );
}
