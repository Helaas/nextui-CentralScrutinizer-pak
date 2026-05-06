import { useEffect, useState } from "react";

import { computeUploadPath, type ParsedZipPreview, uploadPathsFromZip } from "../lib/zip-upload";
import type { ExtractStrategy, UploadPreviewConflict, UploadPreviewResponse, ZipExtractOptions } from "../lib/types";

const PREVIEW_LIMIT = 5;
const STRATEGY_ORDER: ExtractStrategy[] = ["extract-here", "extract-into-folder", "preserve-full-path"];

type ZipExtractOption = {
  description: string;
  previewPaths: string[];
  signature: string;
  strategy: ExtractStrategy;
  title: string;
};

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

function getUploadSignature(preview: ParsedZipPreview, strategy: ExtractStrategy): string {
  const { directories, filePaths } = uploadPathsFromZip(preview, strategy);
  const directoryKeys = directories.map((path) => `d:${path}`).sort();
  const fileKeys = filePaths.map((path) => `f:${path}`).sort();

  return [...directoryKeys, ...fileKeys].join("\n");
}

function getStrategyTitle(strategy: ExtractStrategy, wrapperName: string): string {
  if (strategy === "extract-here") {
    return "Extract here";
  }
  if (strategy === "extract-into-folder") {
    return `Extract into folder "${wrapperName}"`;
  }

  return "Preserve full archive path";
}

function getStrategyDescription(strategy: ExtractStrategy): string {
  if (strategy === "extract-here") {
    return "Place files directly in the current folder";
  }
  if (strategy === "extract-into-folder") {
    return "Wrap contents under a new folder named after the archive";
  }

  return "Keep top-level folders like Tools/ exactly as stored in the archive";
}

function getVisibleOptions(preview: ParsedZipPreview): ZipExtractOption[] {
  const seenSignatures = new Set<string>();
  const options = STRATEGY_ORDER.map((strategy) => {
    const signature = getUploadSignature(preview, strategy);

    return {
      description: getStrategyDescription(strategy),
      previewPaths: getPreviewPaths(preview, strategy),
      signature,
      strategy,
      title: getStrategyTitle(strategy, preview.zipNameWithoutExtension),
    };
  });

  return options.filter((option) => {
    if (seenSignatures.has(option.signature)) {
      return false;
    }

    seenSignatures.add(option.signature);
    return true;
  });
}

function getCanonicalStrategy(preview: ParsedZipPreview, visibleOptions: ZipExtractOption[], strategy: ExtractStrategy): ExtractStrategy {
  const visibleStrategy = visibleOptions.find((option) => option.strategy === strategy);

  if (visibleStrategy) {
    return visibleStrategy.strategy;
  }

  const currentSignature = getUploadSignature(preview, strategy);
  const duplicateOption = visibleOptions.find((option) => option.signature === currentSignature);

  return duplicateOption?.strategy ?? visibleOptions[0]?.strategy ?? strategy;
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
  const visibleOptions = getVisibleOptions(preview);
  const selectedStrategy = getCanonicalStrategy(preview, visibleOptions, strategy);
  const [showMobileSelectedPreview, setShowMobileSelectedPreview] = useState(false);
  const totalRemaining = Math.max(0, preview.entries.length - PREVIEW_LIMIT);
  const overwriteableRemaining = Math.max(0, (conflicts?.overwriteableCount ?? 0) - (conflicts?.overwriteable.length ?? 0));
  const blockingRemaining = Math.max(0, (conflicts?.blockingCount ?? 0) - (conflicts?.blocking.length ?? 0));

  useEffect(() => {
    setShowMobileSelectedPreview(false);
  }, [selectedStrategy]);

  return (
    <div
      aria-labelledby="zip-extract-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-start justify-center overflow-y-auto bg-black/60 p-3 sm:items-center sm:p-4"
      role="dialog"
    >
      <div className="flex w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-xl">
        <div className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-4 py-3 sm:px-5 sm:py-4">
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

        <div className="flex-1 overflow-auto px-4 py-3 sm:px-5 sm:py-4">
          <p className="mb-2 text-sm text-[var(--text)] sm:mb-3">How would you like to extract the contents?</p>

          {visibleOptions.map((option, index) => {
              const mobilePreviewRemaining = Math.max(0, option.previewPaths.length - 1) + totalRemaining;
              const canExpandMobilePreview = option.previewPaths.length > 1 || totalRemaining > 0;
              const isSelected = selectedStrategy === option.strategy;

              return (
                <label
                  key={option.strategy}
                  className={`${index < visibleOptions.length - 1 ? "mb-2 sm:mb-3 " : ""}block cursor-pointer rounded-xl border p-3 transition sm:p-4 ${
                    isSelected
                      ? "border-[var(--accent)] bg-[var(--accent)]/10"
                      : "border-[var(--border)] hover:border-[var(--accent)]/30"
                  }`}
                >
                  <div className="flex items-start gap-3">
                    <input
                      checked={isSelected}
                      className="mt-0.5 h-4 w-4 accent-[var(--accent)]"
                      disabled={checking}
                      name="extract-strategy"
                      onChange={() => onStrategyChange(option.strategy)}
                      type="radio"
                    />
                    <div className="min-w-0 flex-1">
                      <p className="text-sm font-medium leading-5 text-[var(--text)]">{option.title}</p>
                      <p className="mt-1 text-xs text-[var(--muted)]">{option.description}</p>
                      {option.previewPaths[0] ? (
                        <div className="mt-2 sm:hidden">
                          <p className="truncate font-mono text-xs text-[var(--muted)]">{option.previewPaths[0]}</p>
                          {mobilePreviewRemaining > 0 ? (
                            <p className="text-xs text-[var(--muted)]">...and {mobilePreviewRemaining} more</p>
                          ) : null}
                        </div>
                      ) : null}
                    </div>
                  </div>

                  <div className="mt-2 hidden space-y-0.5 pl-7 sm:block">
                    {option.previewPaths.map((path, pathIndex) => (
                      <p key={`${path}:${pathIndex}`} className="truncate font-mono text-xs text-[var(--muted)]">
                        {path}
                      </p>
                    ))}
                    {totalRemaining > 0 ? <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p> : null}
                  </div>

                  {isSelected && canExpandMobilePreview ? (
                    <div className="mt-2 pl-7 sm:hidden">
                      <button
                        aria-expanded={showMobileSelectedPreview}
                        className="text-xs font-medium text-[var(--accent)] transition hover:text-[var(--accent-strong)]"
                        onClick={(event) => {
                          event.preventDefault();
                          event.stopPropagation();
                          setShowMobileSelectedPreview((value) => !value);
                        }}
                        type="button"
                      >
                        {showMobileSelectedPreview ? "Hide sample paths" : "Show sample paths"}
                      </button>
                      {showMobileSelectedPreview ? (
                        <div className="mt-2 space-y-0.5">
                          {option.previewPaths.map((path, pathIndex) => (
                            <p key={`${path}:${pathIndex}`} className="truncate font-mono text-xs text-[var(--muted)]">
                              {path}
                            </p>
                          ))}
                          {totalRemaining > 0 ? (
                            <p className="text-xs text-[var(--muted)]">...and {totalRemaining} more</p>
                          ) : null}
                        </div>
                      ) : null}
                    </div>
                  ) : null}
                </label>
              );
            })}

          <label className="mt-3 flex items-start gap-3 rounded-xl border border-[var(--border)] p-3 sm:mt-4 sm:p-4">
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
            <section className="mt-3 rounded-xl border border-amber-300/25 bg-amber-500/10 p-3 text-sm text-[var(--text)] sm:mt-4 sm:p-4">
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
            <section className="mt-3 rounded-xl border border-[var(--accent)]/30 bg-[var(--accent)]/10 p-3 text-sm text-[var(--text)] sm:mt-4 sm:p-4">
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

        <div className="flex flex-col gap-2 border-t border-[var(--line)] px-4 py-3 sm:flex-row sm:items-center sm:justify-end sm:px-5 sm:py-4">
          <button
            className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-3 py-2 text-xs font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:opacity-50"
            onClick={onCancel}
            type="button"
          >
            Cancel
          </button>
          <button
            className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-70"
            disabled={checking}
            onClick={() => onConfirm({ strategy: selectedStrategy, overwriteExisting })}
            type="button"
          >
            {checking ? "Checking..." : "Extract"}
          </button>
        </div>
      </div>
    </div>
  );
}
