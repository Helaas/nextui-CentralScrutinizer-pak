export function BrowserToolbar({
  busy,
  onCreateFolder,
  onRefresh,
  onUpload,
}: {
  busy?: boolean;
  onCreateFolder: () => void;
  onRefresh: () => void;
  onUpload: () => void;
}) {
  return (
    <div className="flex flex-wrap items-center gap-2">
      <button
        className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
        disabled={busy}
        onClick={onUpload}
        type="button"
      >
        Upload
      </button>
      <button
        className="rounded-md border border-[var(--border)] bg-[var(--panel)] px-4 py-2 text-sm font-medium text-[var(--muted)] transition hover:border-[var(--accent)]/50 hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-50"
        disabled={busy}
        onClick={onCreateFolder}
        type="button"
      >
        New Folder
      </button>
      <button
        className="rounded-md border border-[var(--border)] bg-[var(--panel)] px-4 py-2 text-sm font-medium text-[var(--muted)] transition hover:border-[var(--accent)]/50 hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-50"
        disabled={busy}
        onClick={onRefresh}
        type="button"
      >
        Refresh
      </button>
    </div>
  );
}
