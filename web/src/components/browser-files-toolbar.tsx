import type { BrowserResponse } from "../lib/types";
import { Breadcrumbs } from "./breadcrumbs";

export function BrowserFilesToolbar({
  canRunSearch = false,
  busy,
  canUploadFolder = false,
  onClearSearch,
  onCreateFolder,
  onNavigate,
  onRefresh,
  onRunSearch,
  onSearchChange,
  onUploadFolder,
  onUploadZip,
  onUploadFile,
  response,
  searchResultsActive = false,
  search,
}: {
  canRunSearch?: boolean;
  busy: boolean;
  canUploadFolder?: boolean;
  onClearSearch?: () => void;
  onCreateFolder: () => void;
  onNavigate: (path?: string) => void;
  onRefresh: () => void;
  onRunSearch?: () => void;
  onSearchChange: (value: string) => void;
  onUploadFolder?: () => void;
  onUploadZip?: () => void;
  onUploadFile: () => void;
  response: BrowserResponse;
  searchResultsActive?: boolean;
  search: string;
}) {
  return (
    <section className="rounded-[20px] border border-[var(--border)] bg-[var(--panel)] px-4 py-4">
      <div className="flex flex-col gap-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <Breadcrumbs ariaLabel="Files path" items={response.breadcrumbs} onSelect={onNavigate} rootLabel="SD Card" />
          <div className="flex flex-wrap items-center gap-2">
            <button
              className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onUploadFile}
              type="button"
            >
              Upload File
            </button>
            {canUploadFolder && onUploadFolder ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onUploadFolder}
                type="button"
              >
                Upload Folder
              </button>
            ) : null}
            {onUploadZip ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onUploadZip}
                type="button"
              >
                Upload ZIP
              </button>
            ) : null}
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onCreateFolder}
              type="button"
            >
              New Folder
            </button>
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onRefresh}
              type="button"
            >
              Refresh
            </button>
            {onRunSearch ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy || !canRunSearch}
                onClick={onRunSearch}
                type="button"
              >
                Search Tree
              </button>
            ) : null}
            {searchResultsActive && onClearSearch ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50"
                onClick={onClearSearch}
                type="button"
              >
                Clear Results
              </button>
            ) : null}
          </div>
        </div>
        <div>
          <input
            aria-label="Search current folder"
            className="h-11 w-full rounded-xl border border-[var(--border)] bg-[var(--bg)] px-4 text-sm text-[var(--text)] outline-none focus:border-[var(--accent)]"
            onChange={(event) => {
              onSearchChange(event.target.value);
            }}
            placeholder="Search in current folder"
            value={search}
          />
        </div>
      </div>
    </section>
  );
}
