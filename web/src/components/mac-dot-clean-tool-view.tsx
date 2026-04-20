"use client";

import { useEffect, useState } from "react";

import { deleteItem, getMacDotfiles } from "../lib/api";
import type { MacDotfileEntry } from "../lib/types";

function formatDate(value: number): string {
  if (!value) {
    return "—";
  }

  return new Date(value * 1000).toLocaleString();
}

function formatSize(size: number): string {
  if (!size) {
    return "0 B";
  }

  const units = ["B", "KB", "MB", "GB"];
  const index = Math.min(Math.floor(Math.log(size) / Math.log(1024)), units.length - 1);
  const value = size / 1024 ** index;
  return `${value.toFixed(index === 0 ? 0 : value >= 10 ? 0 : 1)} ${units[index]}`;
}

export function MacDotCleanToolView({
  csrf,
  onBack,
}: {
  csrf: string | null;
  onBack: () => void;
}) {
  const [entries, setEntries] = useState<MacDotfileEntry[]>([]);
  const [entryCount, setEntryCount] = useState(0);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [cleaning, setCleaning] = useState(false);
  const [truncated, setTruncated] = useState(false);

  async function loadEntries() {
    if (!csrf) {
      setEntries([]);
      setEntryCount(0);
      setTruncated(false);
      setNotice("Missing session csrf token.");
      setLoading(false);
      return;
    }

    setLoading(true);
    setNotice(null);
    try {
      const response = await getMacDotfiles(csrf);

      setEntries(response.entries);
      setEntryCount(response.count);
      setTruncated(response.truncated);
    } catch (error) {
      setEntries([]);
      setEntryCount(0);
      setTruncated(false);
      setNotice(error instanceof Error ? error.message : "Could not scan for macOS dotfiles.");
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void loadEntries();
  }, [csrf]);

  async function handleCleanNow() {
    if (!csrf || entries.length === 0) {
      return;
    }
    if (
      !window.confirm(
        truncated
          ? `Delete the ${entries.length} listed macOS transfer artifact${entries.length === 1 ? "" : "s"} now? The scan found ${entryCount} total.`
          : `Delete ${entries.length} macOS transfer artifact${entries.length === 1 ? "" : "s"}?`,
      )
    ) {
      return;
    }

    setCleaning(true);
    setNotice(null);
    try {
      const results = await Promise.allSettled(
        entries.map(async (entry) => deleteItem({ scope: "files", path: entry.path }, csrf)),
      );
      const successCount = results.filter((result) => result.status === "fulfilled").length;
      const failureCount = entries.length - successCount;

      await loadEntries();

      if (failureCount === 0) {
        setNotice(`Deleted ${successCount} macOS artifact${successCount === 1 ? "" : "s"}.`);
        return;
      }
      if (successCount === 0) {
        setNotice(`Failed to delete ${entries.length} macOS artifact${entries.length === 1 ? "" : "s"}.`);
        return;
      }

      setNotice(`Deleted ${successCount} of ${entries.length} macOS artifacts. ${failureCount} failed.`);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Cleanup failed.");
    } finally {
      setCleaning(false);
    }
  }

  const primaryActionClass =
    "rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50";
  const secondaryActionClass =
    "inline-flex min-h-11 items-center justify-center rounded-md border border-[var(--border)] px-3 py-2 text-sm font-medium leading-none text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:opacity-50";

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <button
            className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onBack}
            type="button"
          >
            <span aria-hidden="true">←</span>
            Back
          </button>
          <h2 className="mt-4 text-lg font-semibold">Mac Dot Cleanup</h2>
          <p className="mt-1 max-w-2xl text-sm text-[var(--muted)]">
            Scan the SD card for safe macOS transfer artifacts including `.DS_Store`, AppleDouble `._*` sidecars,
            Spotlight or Finder metadata folders, and `__MACOSX` directories. Matches inside `__MACOSX` are removed
            along with the folder.
          </p>
        </div>
        <div className="flex flex-wrap gap-2">
          <button
            className={secondaryActionClass}
            disabled={loading || cleaning || !csrf}
            onClick={() => {
              void loadEntries();
            }}
            type="button"
          >
            Refresh
          </button>
          <button
            className={primaryActionClass}
            disabled={loading || cleaning || entries.length === 0}
            onClick={() => {
              void handleCleanNow();
            }}
            type="button"
          >
            {cleaning ? "Cleaning..." : "Clean Now"}
          </button>
        </div>
      </div>

      {notice ? (
        <div className="rounded-xl border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}

      {!loading && truncated ? (
        <div className="rounded-2xl border border-amber-300/30 bg-amber-200/10 px-4 py-3 text-sm text-amber-50">
          Showing {entries.length} of {entryCount} macOS transfer artifacts. Clean Now only removes the listed items;
          refresh after cleanup to scan for the rest.
        </div>
      ) : null}

      {loading ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          Scanning for macOS dotfiles...
        </div>
      ) : entries.length === 0 ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          No macOS transfer artifacts found.
        </div>
      ) : (
        <div className="space-y-3">
          {entries.map((entry) => (
            <article
              key={entry.path}
              className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-4"
            >
              <div className="flex flex-wrap items-start justify-between gap-3">
                <div>
                  <p className="break-all font-medium">{entry.path}</p>
                  <p className="mt-1 text-sm text-[var(--muted)]">{entry.reason}</p>
                </div>
                <span className="rounded-full border border-[var(--border)] px-3 py-1 text-xs uppercase tracking-[0.14em] text-[var(--muted)]">
                  {entry.kind}
                </span>
              </div>
              <dl className="mt-4 grid gap-3 text-sm text-[var(--muted)] sm:grid-cols-2 xl:grid-cols-4">
                <div>
                  <dt className="text-xs uppercase tracking-[0.14em]">Modified</dt>
                  <dd className="mt-1 text-[var(--text)]">{formatDate(entry.modified)}</dd>
                </div>
                <div>
                  <dt className="text-xs uppercase tracking-[0.14em]">Size</dt>
                  <dd className="mt-1 text-[var(--text)]">{formatSize(entry.size)}</dd>
                </div>
              </dl>
            </article>
          ))}
        </div>
      )}
    </div>
  );
}
