import { useState } from "react";

import type { ExtractStrategy } from "../lib/types";
import { computeUploadPath, type ParsedZipPreview } from "../lib/zip-upload";

const PREVIEW_LIMIT = 5;

function getPreviewPaths(preview: ParsedZipPreview, strategy: ExtractStrategy): string[] {
  return preview.entries
    .slice(0, PREVIEW_LIMIT)
    .map((entry) => computeUploadPath(entry.path, preview, strategy))
    .filter(Boolean);
}

type ZipExtractDialogProps = {
  preview: ParsedZipPreview;
  onCancel: () => void;
  onConfirm: (strategy: ExtractStrategy) => void;
};

export function ZipExtractDialog({ preview, onCancel, onConfirm }: ZipExtractDialogProps) {
  const [strategy, setStrategy] = useState<ExtractStrategy>("extract-into-folder");

  const herePreview = getPreviewPaths(preview, "extract-here");
  const folderPreview = getPreviewPaths(preview, "extract-into-folder");
  const totalRemaining = Math.max(0, preview.entries.length - PREVIEW_LIMIT);
  const wrapperName = preview.zipNameWithoutExtension;

  return (
    <div
      aria-labelledby="zip-extract-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      role="dialog"
    >
      <div className="flex w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-xl">
        <div className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <div className="min-w-0">
            <h2 className="truncate text-base font-semibold text-[var(--text)]" id="zip-extract-title">
              Extract ZIP
            </h2>
            <p className="truncate text-xs text-[var(--muted)]">{wrapperName}.zip</p>
          </div>
          <button
            aria-label="Close dialog"
            className="rounded-md px-2 py-1 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onCancel}
            type="button"
          >
            ✕
          </button>
        </div>

        <div className="flex-1 overflow-auto px-5 py-4">
          <p className="mb-3 text-sm text-[var(--text)]">How would you like to extract the contents?</p>

          <label
            className={`mb-3 block cursor-pointer rounded-xl border p-4 transition ${
              strategy === "extract-here"
                ? "border-[var(--accent)] bg-[var(--accent)]/10"
                : "border-[var(--border)] hover:border-[var(--accent)]/30"
            }`}
          >
            <div className="flex items-center gap-3">
              <input
                checked={strategy === "extract-here"}
                className="h-4 w-4 accent-[var(--accent)]"
                name="extract-strategy"
                onChange={() => setStrategy("extract-here")}
                type="radio"
              />
              <div className="flex-1">
                <p className="text-sm font-medium text-[var(--text)]">Extract here</p>
                <p className="text-xs text-[var(--muted)]">Place files directly in the current folder</p>
              </div>
            </div>
            <div className="mt-2 space-y-0.5 pl-7">
              {herePreview.map((path) => (
                <p key={path} className="truncate font-mono text-xs text-[var(--muted)]">
                  {path}
                </p>
              ))}
              {totalRemaining > 0 ? (
                <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p>
              ) : null}
            </div>
          </label>

          <label
            className={`block cursor-pointer rounded-xl border p-4 transition ${
              strategy === "extract-into-folder"
                ? "border-[var(--accent)] bg-[var(--accent)]/10"
                : "border-[var(--border)] hover:border-[var(--accent)]/30"
            }`}
          >
            <div className="flex items-center gap-3">
              <input
                checked={strategy === "extract-into-folder"}
                className="h-4 w-4 accent-[var(--accent)]"
                name="extract-strategy"
                onChange={() => setStrategy("extract-into-folder")}
                type="radio"
              />
              <div className="flex-1">
                <p className="text-sm font-medium text-[var(--text)]">Extract into folder &quot;{wrapperName}&quot;</p>
                <p className="text-xs text-[var(--muted)]">Wrap contents under a new folder named after the ZIP</p>
              </div>
            </div>
            <div className="mt-2 space-y-0.5 pl-7">
              {folderPreview.map((path) => (
                <p key={path} className="truncate font-mono text-xs text-[var(--muted)]">
                  {path}
                </p>
              ))}
              {totalRemaining > 0 ? (
                <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p>
              ) : null}
            </div>
          </label>
        </div>

        <div className="flex flex-col gap-2 border-t border-[var(--line)] px-5 py-4 sm:flex-row sm:items-center sm:justify-end">
          <button
            className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onCancel}
            type="button"
          >
            Cancel
          </button>
          <button
            className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-black transition hover:bg-[var(--accent-strong)]"
            onClick={() => onConfirm(strategy)}
            type="button"
          >
            Extract
          </button>
        </div>
      </div>
    </div>
  );
}
