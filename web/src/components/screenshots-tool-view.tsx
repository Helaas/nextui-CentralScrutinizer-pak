"use client";

import { useEffect, useState } from "react";
import JSZip from "jszip";

import { buildDownloadUrl, deleteItem, getBrowser } from "../lib/api";
import type { BrowserEntry } from "../lib/types";

function isImageEntry(entry: BrowserEntry): boolean {
  return entry.type !== "directory" && /\.(png|jpe?g|bmp|gif|webp)$/i.test(entry.name);
}

function formatDate(value: number): string {
  if (!value) {
    return "—";
  }

  return new Date(value * 1000).toLocaleString();
}

export function ScreenshotsToolView({
  csrf,
  onBack,
}: {
  csrf: string | null;
  onBack: () => void;
}) {
  const [entries, setEntries] = useState<BrowserEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [previewPath, setPreviewPath] = useState<string | null>(null);
  const [downloadingAll, setDownloadingAll] = useState(false);
  const [busyPath, setBusyPath] = useState<string | null>(null);

  const primaryActionClass =
    "rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50";
  const secondaryActionClass =
    "inline-flex min-h-11 flex-1 items-center justify-center rounded-md border border-[var(--border)] px-3 py-2 text-sm font-medium leading-none text-[var(--text)] transition hover:border-[var(--accent)]/50";
  const destructiveActionClass =
    "inline-flex min-h-11 flex-1 items-center justify-center rounded-md border border-rose-300/30 px-3 py-2 text-sm font-medium leading-none text-rose-100 transition hover:border-rose-200/40 disabled:opacity-50";

  useEffect(() => {
    let active = true;

    async function loadScreenshots() {
      setLoading(true);
      setNotice(null);

      try {
        if (!csrf) {
          throw new Error("Missing session csrf token.");
        }

        const response = await getBrowser("files", csrf, undefined, "Screenshots");
        const nextEntries = response.entries.filter(isImageEntry).sort((left, right) => right.modified - left.modified);

        if (active) {
          setEntries(nextEntries);
        }
      } catch (error) {
        if (active) {
          setEntries([]);
          setNotice(error instanceof Error ? error.message : "Could not load screenshots.");
        }
      } finally {
        if (active) {
          setLoading(false);
        }
      }
    }

    void loadScreenshots();
    return () => {
      active = false;
    };
  }, [busyPath, csrf]);

  async function deleteScreenshot(entry: BrowserEntry) {
    if (!csrf || !window.confirm(`Delete ${entry.name}?`)) {
      return;
    }

    setBusyPath(entry.path);
    setNotice(null);
    try {
      await deleteItem({ scope: "files", path: entry.path }, csrf);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Delete failed.");
      setBusyPath(null);
      return;
    }
    setBusyPath(null);
  }

  async function downloadAll() {
    setDownloadingAll(true);
    setNotice(null);

    try {
      const zip = new JSZip();

      for (const entry of entries) {
        const response = await fetch(buildDownloadUrl("files", entry.path, undefined, csrf));

        if (!response.ok) {
          throw new Error(`Could not download ${entry.name}`);
        }
        zip.file(entry.name, await response.arrayBuffer());
      }

      const blob = await zip.generateAsync({ type: "blob" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");

      link.href = url;
      link.download = `central-scrutinizer-screenshots-${new Date().toISOString().replace(/[:.]/g, "-")}.zip`;
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Download failed.");
    } finally {
      setDownloadingAll(false);
    }
  }

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
          <h2 className="mt-4 text-lg font-semibold">Screenshots</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">Browse captures from `Screenshots/` and export them in bulk.</p>
        </div>
        <button
          className={primaryActionClass}
          disabled={entries.length === 0 || downloadingAll}
          onClick={() => {
            void downloadAll();
          }}
          type="button"
        >
          {downloadingAll ? "Creating Zip..." : "Download All"}
        </button>
      </div>

      {notice ? (
        <div className="rounded-xl border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}

      {loading ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          Loading screenshots...
        </div>
      ) : entries.length === 0 ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          No screenshots found.
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-4 lg:grid-cols-4">
          {entries.map((entry) => {
            const url = buildDownloadUrl("files", entry.path, undefined, csrf);

            return (
              <article key={entry.path} className="overflow-hidden rounded-[24px] border border-[var(--border)] bg-[var(--panel)]">
                <button
                  className="block w-full bg-black/30"
                  onClick={() => {
                    setPreviewPath(url);
                  }}
                  type="button"
                >
                  <img alt={entry.name} className="aspect-[4/3] w-full object-cover" src={url} />
                </button>
                <div className="space-y-3 px-4 py-4">
                  <div>
                    <p className="truncate text-sm font-medium">{entry.name}</p>
                    <p className="mt-1 text-xs text-[var(--muted)]">{formatDate(entry.modified)}</p>
                  </div>
                  <div className="flex flex-wrap gap-2">
                    <a
                      className={secondaryActionClass}
                      href={url}
                    >
                      Download
                    </a>
                    <button
                      className={destructiveActionClass}
                      disabled={!csrf || busyPath === entry.path}
                      onClick={() => {
                        void deleteScreenshot(entry);
                      }}
                      type="button"
                    >
                      Delete
                    </button>
                  </div>
                </div>
              </article>
            );
          })}
        </div>
      )}

      {previewPath ? (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4">
          <div className="max-h-[90vh] max-w-6xl overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)]">
            <div className="flex justify-end p-3">
              <button className="rounded-md px-3 py-2 text-sm text-[var(--muted)]" onClick={() => setPreviewPath(null)} type="button">
                Close
              </button>
            </div>
            <img alt="Screenshot preview" className="max-h-[80vh] w-full object-contain" src={previewPath} />
          </div>
        </div>
      ) : null}
    </div>
  );
}
