import type { Breadcrumb, BrowserScope } from "../lib/types";

export function BrowserWorkspaceCard({
  breadcrumbs,
  busy,
  canUploadFolder = false,
  itemCount,
  onBack,
  onCreateFolder,
  onNavigate,
  onRefresh,
  onSearchChange,
  onUploadFolder,
  onUploadFile,
  rootLabel,
  scope,
  search,
  title,
}: {
  breadcrumbs: Breadcrumb[];
  busy: boolean;
  canUploadFolder?: boolean;
  itemCount: number;
  onBack: () => void;
  onCreateFolder: () => void;
  onNavigate: (path?: string) => void;
  onRefresh: () => void;
  onSearchChange: (value: string) => void;
  onUploadFolder?: () => void;
  onUploadFile: () => void;
  rootLabel: string;
  scope: BrowserScope;
  search: string;
  title: string;
}) {
  const showFolderOps = scope === "roms";
  const itemCountLabel = `${itemCount.toLocaleString()} item${itemCount === 1 ? "" : "s"}`;

  return (
    <section className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-5">
      <div className="flex flex-col gap-5">
        <div className="flex flex-col gap-5 xl:flex-row xl:items-start xl:justify-between">
          <div className="min-w-0 space-y-4">
            <button
              className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={onBack}
              type="button"
            >
              <span aria-hidden="true">←</span>
              Back
            </button>
            <div className="min-w-0">
              <p className="text-[11px] font-semibold uppercase tracking-[0.22em] text-[var(--muted)]">Library Browser</p>
              <div className="mt-2 flex flex-wrap items-center gap-3">
                <h2 className="text-3xl font-bold tracking-tight">{title}</h2>
                <span className="rounded-full border border-[var(--border)] bg-[var(--bg)] px-3 py-1 text-[11px] font-semibold uppercase tracking-[0.18em] text-[var(--muted)]">
                  {itemCountLabel}
                </span>
              </div>
              <nav
                aria-label="Library path"
                className="mt-3 flex flex-wrap items-center gap-x-2 gap-y-1 text-sm text-[var(--muted)]"
              >
                <button
                  className="transition hover:text-[var(--text)]"
                  onClick={() => {
                    onNavigate(undefined);
                  }}
                  type="button"
                >
                  {rootLabel}
                </button>
                {breadcrumbs.map((item) => (
                  <span key={item.path} className="flex items-center gap-2">
                    <span aria-hidden="true">›</span>
                    <button
                      className="transition hover:text-[var(--text)]"
                      onClick={() => {
                        onNavigate(item.path);
                      }}
                      type="button"
                    >
                      {item.label}
                    </button>
                  </span>
                ))}
              </nav>
            </div>
          </div>

          <div className="flex flex-wrap items-center gap-2 xl:max-w-[24rem] xl:justify-end">
            <button
              className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onUploadFile}
              type="button"
            >
              Upload File
            </button>
            {showFolderOps && canUploadFolder && onUploadFolder ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onUploadFolder}
                type="button"
              >
                Upload Folder
              </button>
            ) : null}
            {showFolderOps ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onCreateFolder}
                type="button"
              >
                New Folder
              </button>
            ) : null}
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onRefresh}
              type="button"
            >
              Refresh
            </button>
          </div>
        </div>

        <div className="relative">
          <svg
            aria-hidden="true"
            className="pointer-events-none absolute left-4 top-1/2 h-4 w-4 -translate-y-1/2 text-[var(--muted)]"
            fill="none"
            viewBox="0 0 24 24"
          >
            <circle cx="11" cy="11" r="6" stroke="currentColor" strokeWidth="1.8" />
            <path d="M16 16l4 4" stroke="currentColor" strokeLinecap="round" strokeWidth="1.8" />
          </svg>
          <input
            aria-label="Search current folder"
            className="h-12 w-full rounded-xl border border-[var(--border)] bg-[var(--bg)] pl-11 pr-4 text-sm text-[var(--text)] outline-none transition focus:border-[var(--accent)]"
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
