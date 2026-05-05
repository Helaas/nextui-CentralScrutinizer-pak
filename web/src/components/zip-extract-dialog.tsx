import { computeUploadPath, type ParsedZipPreview } from "../lib/zip-upload";
import type { ExtractStrategy, UploadPreviewConflict, UploadPreviewResponse, ZipExtractOptions } from "../lib/types";

const PREVIEW_LIMIT = 5;

function getPreviewPaths(preview: ParsedZipPreview, strategy: ExtractStrategy): string[] {
  return preview.entries
    .slice(0, PREVIEW_LIMIT)
    .map((entry) => computeUploadPath(entry.path, preview, strategy))
    .filter(Boolean);
}

function describeConflict(conflict: UploadPreviewConflict): string {
  if (conflict.kind === "directory-over-file") {
    return `Folder needed but a file already exists: ${conflict.path}`;
  }
  if (conflict.kind === "file-over-directory") {
    return `File needed but a folder already exists: ${conflict.path}`;
  }

  return `Existing file would be replaced: ${conflict.path}`;
}

type ZipExtractDialogProps = {
  preview: ParsedZipPreview;
  strategy: ExtractStrategy;
  overwriteExisting: boolean;
  conflicts?: UploadPreviewResponse | null;
  checking?: boolean;
  onStrategyChange: (strategy: ExtractStrategy) => void;
  onOverwriteChange: (value: boolean) => void;
  onCancel: () => void;
  onConfirm: (options: ZipExtractOptions) => void;
};

export function ZipExtractDialog({
  preview,
  strategy,
  overwriteExisting,
  conflicts,
  checking = false,
  onStrategyChange,
  onOverwriteChange,
  onCancel,
  onConfirm,
}: ZipExtractDialogProps) {
  const herePreview = getPreviewPaths(preview, "extract-here");
  const folderPreview = getPreviewPaths(preview, "extract-into-folder");
  const preservePreview = getPreviewPaths(preview, "preserve-full-path");
  const totalRemaining = Math.max(0, preview.entries.length - PREVIEW_LIMIT);
  const wrapperName = preview.zipNameWithoutExtension;
  const overwriteableRemaining = Math.max(0, (conflicts?.overwriteableCount ?? 0) - (conflicts?.overwriteable.length ?? 0));
  const blockingRemaining = Math.max(0, (conflicts?.blockingCount ?? 0) - (conflicts?.blocking.length ?? 0));

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
            <p className="truncate text-xs text-[var(--muted)]">{preview.archiveFileName}</p>
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
                disabled={checking}
                name="extract-strategy"
                onChange={() => onStrategyChange("extract-here")}
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
              {totalRemaining > 0 ? <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p> : null}
            </div>
          </label>

          <label
            className={`mb-3 block cursor-pointer rounded-xl border p-4 transition ${
              strategy === "extract-into-folder"
                ? "border-[var(--accent)] bg-[var(--accent)]/10"
                : "border-[var(--border)] hover:border-[var(--accent)]/30"
            }`}
          >
            <div className="flex items-center gap-3">
              <input
                checked={strategy === "extract-into-folder"}
                className="h-4 w-4 accent-[var(--accent)]"
                disabled={checking}
                name="extract-strategy"
                onChange={() => onStrategyChange("extract-into-folder")}
                type="radio"
              />
              <div className="flex-1">
                <p className="text-sm font-medium text-[var(--text)]">Extract into folder &quot;{wrapperName}&quot;</p>
                <p className="text-xs text-[var(--muted)]">Wrap contents under a new folder named after the archive</p>
              </div>
            </div>
            <div className="mt-2 space-y-0.5 pl-7">
              {folderPreview.map((path) => (
                <p key={path} className="truncate font-mono text-xs text-[var(--muted)]">
                  {path}
                </p>
              ))}
              {totalRemaining > 0 ? <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p> : null}
            </div>
          </label>

          <label
            className={`block cursor-pointer rounded-xl border p-4 transition ${
              strategy === "preserve-full-path"
                ? "border-[var(--accent)] bg-[var(--accent)]/10"
                : "border-[var(--border)] hover:border-[var(--accent)]/30"
            }`}
          >
            <div className="flex items-center gap-3">
              <input
                checked={strategy === "preserve-full-path"}
                className="h-4 w-4 accent-[var(--accent)]"
                disabled={checking}
                name="extract-strategy"
                onChange={() => onStrategyChange("preserve-full-path")}
                type="radio"
              />
              <div className="flex-1">
                <p className="text-sm font-medium text-[var(--text)]">Preserve full archive path</p>
                <p className="text-xs text-[var(--muted)]">Keep top-level folders like Tools/ exactly as stored in the archive</p>
              </div>
            </div>
            <div className="mt-2 space-y-0.5 pl-7">
              {preservePreview.map((path) => (
                <p key={path} className="truncate font-mono text-xs text-[var(--muted)]">
                  {path}
                </p>
              ))}
              {totalRemaining > 0 ? <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p> : null}
            </div>
          </label>

          <label className="mt-4 flex items-start gap-3 rounded-xl border border-[var(--border)] p-4">
            <input
              checked={overwriteExisting}
              className="mt-0.5 h-4 w-4 accent-[var(--accent)]"
              disabled={checking}
              onChange={(event) => onOverwriteChange(event.target.checked)}
              type="checkbox"
            />
            <div>
              <p className="text-sm font-medium text-[var(--text)]">Allow overwriting existing files</p>
              <p className="text-xs text-[var(--muted)]">
                Off by default. Existing folders merge automatically, but file and folder type conflicts still block extraction.
              </p>
            </div>
          </label>

          {conflicts && (conflicts.overwriteableCount > 0 || conflicts.blockingCount > 0) ? (
            <section className="mt-4 rounded-xl border border-amber-300/25 bg-amber-500/10 p-4 text-sm text-[var(--text)]">
              <p className="font-semibold">
                {conflicts.blockingCount > 0
                  ? "Some paths need attention before extraction can continue."
                  : "This extraction would replace existing files."}
              </p>

              {conflicts.overwriteableCount > 0 ? (
                <div className="mt-3">
                  <p className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                    Replaceable file conflicts ({conflicts.overwriteableCount})
                  </p>
                  <div className="mt-2 space-y-1">
                    {conflicts.overwriteable.map((conflict) => (
                      <p key={`${conflict.kind}:${conflict.path}`} className="break-all font-mono text-xs text-[var(--muted)]">
                        {describeConflict(conflict)}
                      </p>
                    ))}
                    {overwriteableRemaining > 0 ? (
                      <p className="text-xs text-[var(--muted)]">...and {overwriteableRemaining} more</p>
                    ) : null}
                  </div>
                  {!overwriteExisting ? (
                    <p className="mt-2 text-xs text-[var(--muted)]">Enable overwrite to replace these existing files.</p>
                  ) : null}
                </div>
              ) : null}

              {conflicts.blockingCount > 0 ? (
                <div className="mt-3">
                  <p className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                    Blocking type conflicts ({conflicts.blockingCount})
                  </p>
                  <div className="mt-2 space-y-1">
                    {conflicts.blocking.map((conflict) => (
                      <p key={`${conflict.kind}:${conflict.path}`} className="break-all font-mono text-xs text-[var(--muted)]">
                        {describeConflict(conflict)}
                      </p>
                    ))}
                    {blockingRemaining > 0 ? (
                      <p className="text-xs text-[var(--muted)]">...and {blockingRemaining} more</p>
                    ) : null}
                  </div>
                  <p className="mt-2 text-xs text-[var(--muted)]">
                    These conflicts need a different destination or manual cleanup before extraction can continue.
                  </p>
                </div>
              ) : null}
            </section>
          ) : null}

          {checking ? (
            <section className="mt-4 rounded-xl border border-[var(--accent)]/30 bg-[var(--accent)]/10 p-4 text-sm text-[var(--text)]">
              <div className="flex items-start gap-3">
                <span
                  aria-hidden="true"
                  className="mt-0.5 inline-block h-3.5 w-3.5 animate-spin rounded-full border-2 border-[var(--accent)] border-t-transparent"
                />
                <div>
                  <p className="font-medium">Checking destination for conflicts...</p>
                  <p className="text-xs text-[var(--muted)]">
                    Extraction stays paused until the preview scan finishes.
                  </p>
                </div>
              </div>
            </section>
          ) : null}
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
            className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-black transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-70"
            disabled={checking}
            onClick={() => onConfirm({ strategy, overwriteExisting })}
            type="button"
          >
            {checking ? "Checking..." : "Extract"}
          </button>
        </div>
      </div>
    </div>
  );
}
