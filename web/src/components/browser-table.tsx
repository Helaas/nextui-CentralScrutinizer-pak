import type { ReactElement } from "react";
import { useEffect, useRef, useState } from "react";

import { buildDownloadUrl } from "../lib/api";
import { isPlaintextFileName } from "../lib/plaintext";
import type { BrowserEntry, BrowserScope } from "../lib/types";

const DASH = "\u2014";

function formatSize(size: number): string {
  if (!size) {
    return DASH;
  }

  const units = ["B", "KB", "MB", "GB"];
  const index = Math.min(Math.floor(Math.log(size) / Math.log(1024)), units.length - 1);
  const value = size / 1024 ** index;
  return `${value.toFixed(index === 0 ? 0 : 1)} ${units[index]}`;
}

function formatDate(value: number): string {
  if (!value) {
    return DASH;
  }

  return new Date(value * 1000).toLocaleString();
}

function FolderGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-5 w-5 shrink-0">
      <path
        fill="currentColor"
        d="M3.5 7.25A2.25 2.25 0 0 1 5.75 5h3.94c.6 0 1.17.24 1.59.66L12.41 7H18.25A2.75 2.75 0 0 1 21 9.75v7.5A2.75 2.75 0 0 1 18.25 20h-12.5A2.75 2.75 0 0 1 3 17.25v-10z"
      />
    </svg>
  );
}

function FileGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-5 w-5 shrink-0">
      <path
        fill="currentColor"
        d="M7.25 3.75A2.75 2.75 0 0 0 4.5 6.5v11A2.75 2.75 0 0 0 7.25 20.25h9.5A2.75 2.75 0 0 0 19.5 17.5V9.2a2.75 2.75 0 0 0-.8-1.95l-2.65-2.7a2.75 2.75 0 0 0-1.97-.8z"
      />
      <path fill="currentColor" d="M14 4.75V8h3.25z" />
    </svg>
  );
}

function TrashGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-3.5 w-3.5">
      <path
        d="M4 7h16M9 7V5a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2m1 0v12a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V7h12z"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth="1.8"
      />
    </svg>
  );
}

function PencilGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-3.5 w-3.5">
      <path
        d="M4 20h4l10-10-4-4L4 16v4zm11-13l3 3"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth="1.8"
      />
    </svg>
  );
}

function MoreGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-4 w-4">
      <circle cx="5" cy="12" r="1.7" fill="currentColor" />
      <circle cx="12" cy="12" r="1.7" fill="currentColor" />
      <circle cx="19" cy="12" r="1.7" fill="currentColor" />
    </svg>
  );
}

function ImageGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-5 w-5 shrink-0">
      <path
        d="M4 5h16a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1zm1 13 4.5-5 3.5 4 2.5-3 4 4"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeLinejoin="round"
        strokeWidth="1.6"
      />
      <circle cx="9" cy="10" r="1.5" fill="currentColor" />
    </svg>
  );
}

function CartridgeGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-5 w-5 shrink-0">
      <path
        fill="currentColor"
        d="M6 3h9.2a2 2 0 0 1 1.4.6l2.8 2.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H10v-3H8v3H6a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"
      />
      <rect x="7" y="6" width="9" height="4" rx="0.8" fill="rgba(0,0,0,0.35)" />
    </svg>
  );
}

function ChipGlyph() {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24" className="h-5 w-5 shrink-0">
      <rect x="6" y="6" width="12" height="12" rx="1.5" fill="currentColor" />
      <g stroke="currentColor" strokeWidth="1.4" strokeLinecap="round">
        <path d="M9 3v3M12 3v3M15 3v3M9 18v3M12 18v3M15 18v3M3 9h3M3 12h3M3 15h3M18 9h3M18 12h3M18 15h3" />
      </g>
    </svg>
  );
}

type ScopeVisual = {
  label: string;
  Glyph: () => ReactElement;
  tone: "accent" | "muted";
};

function scopeVisual(scope: BrowserScope, entryType: string): ScopeVisual {
  if (entryType === "directory") {
    return { label: "DIR", Glyph: FolderGlyph, tone: "muted" };
  }

  switch (scope) {
    case "roms":
      return { label: "ROM", Glyph: CartridgeGlyph, tone: "accent" };
    case "saves":
      return { label: "SAV", Glyph: FileGlyph, tone: "muted" };
    case "cheats":
      return { label: "CHT", Glyph: FileGlyph, tone: "muted" };
    case "overlays":
      return { label: "OVR", Glyph: ImageGlyph, tone: "muted" };
    case "bios":
      return { label: "BIO", Glyph: ChipGlyph, tone: "muted" };
    default:
      return { label: entryType.slice(0, 3).toUpperCase(), Glyph: FileGlyph, tone: "muted" };
  }
}

function RowGlyph({ scope, entry }: { scope: BrowserScope; entry: BrowserEntry }) {
  const { label, Glyph, tone } = scopeVisual(scope, entry.type);
  const container =
    tone === "accent"
      ? "bg-[var(--accent-soft)] text-[var(--accent)]"
      : "bg-black/15 text-[var(--muted)]";

  return (
    <div
      className={`flex h-12 w-10 flex-col items-center justify-center gap-0.5 rounded-lg ${container}`}
    >
      <Glyph />
      <span className="text-[9px] font-black uppercase tracking-[0.15em]">{label}</span>
    </div>
  );
}

function RowMoreMenu({
  busy,
  canReplaceArt,
  onDelete,
  onRename,
  onReplaceArt,
  entry,
}: {
  busy: boolean;
  canReplaceArt: boolean;
  onDelete?: (entry: BrowserEntry) => void;
  onRename?: (entry: BrowserEntry) => void;
  onReplaceArt?: (entry: BrowserEntry) => void;
  entry: BrowserEntry;
}) {
  const [open, setOpen] = useState(false);
  const wrapperRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    if (!open) {
      return;
    }
    const handle = (event: MouseEvent) => {
      if (!wrapperRef.current?.contains(event.target as Node)) {
        setOpen(false);
      }
    };
    const handleKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setOpen(false);
      }
    };
    document.addEventListener("mousedown", handle);
    document.addEventListener("keydown", handleKey);
    return () => {
      document.removeEventListener("mousedown", handle);
      document.removeEventListener("keydown", handleKey);
    };
  }, [open]);

  if (!onRename && !onDelete && !onReplaceArt) {
    return null;
  }

  return (
    <div className="relative" ref={wrapperRef}>
      <button
        aria-haspopup="menu"
        aria-expanded={open}
        aria-label={`More actions for ${entry.name}`}
        className="inline-flex h-9 w-9 items-center justify-center rounded-md border border-[var(--border)] text-[var(--muted)] transition hover:border-[var(--accent)]/50 hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-40"
        disabled={busy}
        onClick={() => setOpen((current) => !current)}
        type="button"
      >
        <MoreGlyph />
      </button>
      {open ? (
        <div
          role="menu"
          className="absolute right-0 top-full z-20 mt-1 min-w-[11rem] overflow-hidden rounded-xl border border-[var(--border)] bg-[var(--panel)] shadow-[var(--shadow)]"
        >
          {onRename ? (
            <button
              role="menuitem"
              className="flex w-full items-center gap-2 px-4 py-2.5 text-left text-sm text-[var(--text)] transition hover:bg-white/[0.05] disabled:cursor-not-allowed disabled:opacity-40"
              disabled={busy}
              onClick={() => {
                setOpen(false);
                onRename(entry);
              }}
              type="button"
            >
              <PencilGlyph />
              Rename
            </button>
          ) : null}
          {canReplaceArt && onReplaceArt ? (
            <button
              role="menuitem"
              className="flex w-full items-center gap-2 px-4 py-2.5 text-left text-sm text-[var(--text)] transition hover:bg-white/[0.05] disabled:cursor-not-allowed disabled:opacity-40"
              disabled={busy}
              onClick={() => {
                setOpen(false);
                onReplaceArt(entry);
              }}
              type="button"
            >
              <ImageGlyph />
              Replace Art
            </button>
          ) : null}
          {onDelete ? (
            <button
              role="menuitem"
              className="flex w-full items-center gap-2 border-t border-[var(--line)] px-4 py-2.5 text-left text-sm text-rose-200 transition hover:bg-rose-500/10 disabled:cursor-not-allowed disabled:opacity-40"
              disabled={busy}
              onClick={() => {
                setOpen(false);
                onDelete(entry);
              }}
              type="button"
            >
              <TrashGlyph />
              Delete
            </button>
          ) : null}
        </div>
      ) : null}
    </div>
  );
}

function FilesTable({
  busy,
  csrf,
  entries,
  onDelete,
  onEdit,
  onNavigate,
  onNavigateParent,
  onRename,
  tag,
}: {
  busy: boolean;
  csrf?: string | null;
  entries: BrowserEntry[];
  onDelete?: (entry: BrowserEntry) => void;
  onEdit?: (entry: BrowserEntry) => void;
  onNavigate: (path?: string) => void;
  onNavigateParent?: () => void;
  onRename?: (entry: BrowserEntry) => void;
  tag?: string;
}) {
  const gridClass =
    "grid grid-cols-[minmax(0,1fr),auto] gap-3 md:grid-cols-[minmax(0,1fr)_100px_220px_260px] md:gap-4";

  return (
    <div className="overflow-hidden rounded-[24px] border border-[var(--border)] bg-[var(--panel)]">
      <div
        className={`hidden border-b border-[var(--line)] bg-white/[0.02] px-4 py-3 text-xs font-semibold uppercase tracking-wider text-[var(--muted)] md:grid ${gridClass}`}
      >
        <span className="flex items-center gap-1">
          Name <span aria-hidden="true">▲</span>
        </span>
        <span>Size</span>
        <span>Modified</span>
        <span className="text-right">Action</span>
      </div>

      {onNavigateParent ? (
        <div className={`items-center border-t border-[var(--line)] px-4 py-3 text-sm ${gridClass}`}>
          <button
            aria-label="Go to parent folder"
            className="flex items-center gap-3 text-left italic text-[var(--muted)] transition hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-50"
            disabled={busy}
            onClick={onNavigateParent}
            type="button"
          >
            <svg aria-hidden="true" className="h-4 w-4 shrink-0" fill="none" viewBox="0 0 24 24">
              <path
                d="M15 6l-6 6 6 6"
                stroke="currentColor"
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth="2"
              />
            </svg>
            ..
          </button>
          <span className="hidden text-[var(--muted)] md:block">{DASH}</span>
          <span className="hidden text-[var(--muted)] md:block">{DASH}</span>
          <span className="hidden md:block" />
        </div>
      ) : null}

      {entries.length === 0 && !onNavigateParent ? (
        <div className="px-5 py-10 text-center text-sm italic text-[var(--muted)]">
          No files found in this directory.
        </div>
      ) : (
        entries.map((entry) => {
          const isDir = entry.type === "directory";

          return (
            <div
              key={entry.path}
              className={`group items-center border-t border-[var(--line)] px-4 py-3 text-sm transition hover:bg-white/[0.03] ${gridClass}`}
            >
              <div className="min-w-0">
                {isDir ? (
                  <button
                    aria-label={`Open ${entry.name}`}
                    className="flex w-full min-w-0 items-center gap-3 text-left disabled:cursor-not-allowed disabled:opacity-60"
                    disabled={busy}
                    onClick={() => {
                      onNavigate(entry.path);
                    }}
                    type="button"
                  >
                    <span className="shrink-0 text-[var(--muted)] transition group-hover:text-[var(--accent)]">
                      <FolderGlyph />
                    </span>
                    <span className="truncate font-medium text-[var(--text)] transition group-hover:text-white">
                      {entry.name}
                    </span>
                  </button>
                ) : (
                  <a
                    aria-label={`Download ${entry.name}`}
                    className="flex min-w-0 items-center gap-3"
                    href={buildDownloadUrl("files", entry.path, tag, csrf)}
                  >
                    <span className="shrink-0 text-[var(--muted)] transition group-hover:text-[var(--accent)]">
                      <FileGlyph />
                    </span>
                    <span className="truncate font-medium text-[var(--text)] transition group-hover:text-white">
                      {entry.name}
                    </span>
                  </a>
                )}
              </div>
              <span className="hidden text-[var(--muted)] tabular-nums md:block">
                {isDir ? DASH : formatSize(entry.size)}
              </span>
              <span className="hidden font-mono text-xs tabular-nums text-[var(--muted)] md:block">
                {formatDate(entry.modified)}
              </span>
              <div className="flex flex-wrap justify-end gap-1 whitespace-nowrap md:flex-nowrap">
                {onRename ? (
                  <button
                    aria-label={`Rename ${entry.name}`}
                    className="inline-flex items-center gap-1 rounded-md px-2 py-1 text-xs font-semibold text-[var(--muted)] transition hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-40"
                    disabled={busy}
                    onClick={() => {
                      onRename(entry);
                    }}
                    type="button"
                  >
                    <PencilGlyph />
                    Rename
                  </button>
                ) : null}
                {onEdit && !isDir && isPlaintextFileName(entry.name) ? (
                  <button
                    aria-label={`Edit ${entry.name}`}
                    className="inline-flex items-center gap-1 rounded-md px-2 py-1 text-xs font-semibold text-[var(--muted)] transition hover:text-[var(--text)] disabled:cursor-not-allowed disabled:opacity-40"
                    disabled={busy}
                    onClick={() => {
                      onEdit(entry);
                    }}
                    type="button"
                  >
                    <PencilGlyph />
                    Edit
                  </button>
                ) : null}
                {onDelete ? (
                  <button
                    aria-label={`Delete ${entry.name}`}
                    className="inline-flex items-center gap-1 rounded-md px-2 py-1 text-xs font-semibold text-[var(--accent)] transition hover:text-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-40"
                    disabled={busy}
                    onClick={() => {
                      onDelete(entry);
                    }}
                    type="button"
                  >
                    <TrashGlyph />
                    Delete
                  </button>
                ) : null}
              </div>
            </div>
          );
        })
      )}
    </div>
  );
}

function LibraryTable({
  busy,
  csrf,
  entries,
  onDelete,
  onNavigate,
  onRename,
  onReplaceArt,
  scope,
  tag,
}: {
  busy: boolean;
  csrf?: string | null;
  entries: BrowserEntry[];
  onDelete?: (entry: BrowserEntry) => void;
  onNavigate: (path?: string) => void;
  onRename?: (entry: BrowserEntry) => void;
  onReplaceArt?: (entry: BrowserEntry) => void;
  scope: BrowserScope;
  tag?: string;
}) {
  const gridClass =
    "grid grid-cols-[minmax(0,1fr),auto] gap-3 md:grid-cols-[minmax(0,1fr)_110px_220px_140px] md:gap-4";

  return (
    <div className="relative overflow-visible rounded-[24px] border border-[var(--border)] bg-[var(--panel)]">
      <div
        className={`hidden border-b border-[var(--line)] bg-white/[0.02] px-4 py-3 text-xs font-semibold uppercase tracking-wider text-[var(--muted)] md:grid ${gridClass}`}
      >
        <span>Name</span>
        <span>Size</span>
        <span>Modified</span>
        <span className="text-right">Action</span>
      </div>

      {entries.length === 0 ? (
        <div className="px-5 py-10 text-center text-sm italic text-[var(--muted)]">
          Nothing found in this folder.
        </div>
      ) : (
        entries.map((entry) => {
          const isDir = entry.type === "directory";
          const canReplaceArt = scope === "roms" && entry.type === "rom";

          return (
            <div
              key={entry.path}
              className={`group items-center border-t border-[var(--line)] px-4 py-3 text-sm transition hover:bg-white/[0.03] ${gridClass}`}
            >
              <div className="min-w-0">
                <div className="flex min-w-0 items-center gap-3">
                  <RowGlyph entry={entry} scope={scope} />
                  <div className="min-w-0">
                    <p className="truncate font-medium text-[var(--text)] transition group-hover:text-white">{entry.name}</p>
                    <p className="truncate text-xs text-[var(--muted)] md:hidden">
                      {isDir ? "Folder" : formatSize(entry.size)}
                      {entry.modified ? ` · ${formatDate(entry.modified)}` : ""}
                    </p>
                  </div>
                </div>
              </div>
              <span className="hidden text-[var(--muted)] tabular-nums md:block">
                {isDir ? DASH : formatSize(entry.size)}
              </span>
              <span className="hidden font-mono text-xs tabular-nums text-[var(--muted)] md:block">
                {formatDate(entry.modified)}
              </span>
              <div className="flex items-center justify-end gap-2">
                {isDir ? (
                  <button
                    aria-label={`Open ${entry.name}`}
                    className="rounded-md border border-[var(--border)] px-3 py-2 text-xs font-medium transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                    disabled={busy}
                    onClick={() => {
                      onNavigate(entry.path);
                    }}
                    type="button"
                  >
                    Open
                  </button>
                ) : (
                  <a
                    aria-label={`Download ${entry.name}`}
                    className="rounded-md bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition hover:bg-[var(--accent-strong)]"
                    href={buildDownloadUrl(scope, entry.path, tag, csrf)}
                  >
                    Download
                  </a>
                )}
                <RowMoreMenu
                  busy={busy}
                  canReplaceArt={canReplaceArt}
                  entry={entry}
                  onDelete={onDelete}
                  onRename={onRename}
                  onReplaceArt={onReplaceArt}
                />
              </div>
            </div>
          );
        })
      )}
    </div>
  );
}

export function BrowserTable({
  busy = false,
  csrf,
  entries,
  onDelete,
  onEdit,
  onNavigate,
  onNavigateParent,
  onRename,
  onReplaceArt,
  scope,
  tag,
}: {
  busy?: boolean;
  csrf?: string | null;
  entries: BrowserEntry[];
  onDelete?: (entry: BrowserEntry) => void;
  onEdit?: (entry: BrowserEntry) => void;
  onNavigate: (path?: string) => void;
  onNavigateParent?: () => void;
  onRename?: (entry: BrowserEntry) => void;
  onReplaceArt?: (entry: BrowserEntry) => void;
  scope: BrowserScope;
  tag?: string;
}) {
  if (scope === "files") {
    return (
      <FilesTable
        busy={busy}
        csrf={csrf}
        entries={entries}
        onDelete={onDelete}
        onEdit={onEdit}
        onNavigate={onNavigate}
        onNavigateParent={onNavigateParent}
        onRename={onRename}
        tag={tag}
      />
    );
  }

  return (
    <LibraryTable
      busy={busy}
      csrf={csrf}
      entries={entries}
      onDelete={onDelete}
      onNavigate={onNavigate}
      onRename={onRename}
      onReplaceArt={onReplaceArt}
      scope={scope}
      tag={tag}
    />
  );
}
