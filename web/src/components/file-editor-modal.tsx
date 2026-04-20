import { useEffect, useState } from "react";

import type { BrowserEntry } from "../lib/types";
import { PLAINTEXT_MAX_BYTES } from "../lib/plaintext";

export function FileEditorModal({
  entry,
  initialContent,
  loading,
  loadError,
  saving,
  onCancel,
  onSave,
}: {
  entry: BrowserEntry;
  initialContent: string;
  loading: boolean;
  loadError?: string | null;
  saving: boolean;
  onCancel: () => void;
  onSave: (content: string) => void;
}) {
  const [value, setValue] = useState(initialContent);

  useEffect(() => {
    setValue(initialContent);
  }, [initialContent, entry.path]);

  const byteLength = typeof TextEncoder !== "undefined" ? new TextEncoder().encode(value).length : value.length;
  const overLimit = byteLength > PLAINTEXT_MAX_BYTES;

  return (
    <div
      aria-labelledby="file-editor-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      role="dialog"
    >
      <div className="flex h-full max-h-[85vh] w-full max-w-3xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-xl">
        <div className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <div className="min-w-0">
            <h2 className="truncate text-base font-semibold text-[var(--text)]" id="file-editor-title">
              Edit {entry.name}
            </h2>
            <p className="truncate text-xs text-[var(--muted)]">{entry.path}</p>
          </div>
          <button
            aria-label="Close editor"
            className="rounded-md px-2 py-1 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onCancel}
            type="button"
          >
            ✕
          </button>
        </div>

        <div className="flex-1 overflow-hidden px-5 py-4">
          {loading ? (
            <p className="text-sm italic text-[var(--muted)]">Loading file contents...</p>
          ) : loadError ? (
            <p className="text-sm text-[var(--accent)]">{loadError}</p>
          ) : (
            <textarea
              aria-label={`Edit contents of ${entry.name}`}
              className="h-full w-full resize-none rounded-md border border-[var(--border)] bg-black/20 p-3 font-mono text-xs text-[var(--text)] focus:border-[var(--accent)] focus:outline-none"
              disabled={saving}
              onChange={(event) => setValue(event.target.value)}
              spellCheck={false}
              value={value}
            />
          )}
        </div>

        <div className="flex flex-col gap-2 border-t border-[var(--line)] px-5 py-4 sm:flex-row sm:items-center sm:justify-between">
          <p className={`text-xs ${overLimit ? "text-[var(--accent)]" : "text-[var(--muted)]"}`}>
            {byteLength.toLocaleString()} / {PLAINTEXT_MAX_BYTES.toLocaleString()} bytes
          </p>
          <div className="flex gap-2 sm:justify-end">
            <button
              className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
              disabled={saving}
              onClick={onCancel}
              type="button"
            >
              Cancel
            </button>
            <button
              className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-black transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
              disabled={loading || saving || overLimit || !!loadError}
              onClick={() => onSave(value)}
              type="button"
            >
              {saving ? "Saving..." : "Save"}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
