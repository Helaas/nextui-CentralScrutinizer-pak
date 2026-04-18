"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import JSZip from "jszip";

import { buildLogDownloadUrl, getLogs } from "../lib/api";
import type { LogFileSummary } from "../lib/types";

const MAX_LINES = 2000;

type SortKey = "path" | "size" | "modified";
type SortDirection = "asc" | "desc";

function formatSize(size: number): string {
  if (size < 1024) {
    return `${size} B`;
  }

  const units = ["KB", "MB", "GB"];
  let value = size / 1024;
  let index = 0;

  while (value >= 1024 && index < units.length - 1) {
    value /= 1024;
    index += 1;
  }

  return `${value.toFixed(value >= 10 ? 0 : 1)} ${units[index]}`;
}

function formatDate(timestamp: number): string {
  if (!timestamp) {
    return "—";
  }

  return new Date(timestamp * 1000).toLocaleString();
}

export function LogsToolView({
  csrf,
  initialPath,
  onBack,
  onPathChange,
}: {
  csrf: string | null;
  initialPath?: string;
  onBack: () => void;
  onPathChange: (path?: string) => void;
}) {
  const [files, setFiles] = useState<LogFileSummary[]>([]);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [progress, setProgress] = useState("");
  const [sortKey, setSortKey] = useState<SortKey>("modified");
  const [sortDirection, setSortDirection] = useState<SortDirection>("desc");
  const [viewingPath, setViewingPath] = useState<string | undefined>(initialPath);
  const [logLines, setLogLines] = useState<string[]>([]);
  const [autoScroll, setAutoScroll] = useState(true);
  const [tailError, setTailError] = useState<string | null>(null);
  const [downloadingAll, setDownloadingAll] = useState(false);
  const [downloadingPath, setDownloadingPath] = useState<string | null>(null);
  const tailContainerRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    setViewingPath(initialPath);
  }, [initialPath]);

  useEffect(() => {
    if (!autoScroll || !tailContainerRef.current) {
      return;
    }

    tailContainerRef.current.scrollTop = tailContainerRef.current.scrollHeight;
  }, [autoScroll, logLines]);

  useEffect(() => {
    let cancelled = false;

    async function loadLogs() {
      setLoading(true);
      setNotice(null);
      try {
        if (!csrf) {
          throw new Error("Missing session csrf token.");
        }

        const response = await getLogs(csrf);

        if (!cancelled) {
          setFiles(response.files);
        }
      } catch (error) {
        if (!cancelled) {
          setNotice(error instanceof Error ? error.message : "Logs lookup failed");
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    }

    void loadLogs();
    return () => {
      cancelled = true;
    };
  }, [csrf]);

  useEffect(() => {
    if (!viewingPath) {
      setLogLines([]);
      setTailError(null);
      return;
    }

    const controller = new AbortController();
    const decoder = new TextDecoder();
    let partial = "";
    let active = true;

    setLogLines([]);
    setTailError(null);

    void (async () => {
      try {
        const response = await fetch(`/api/logs/tail?path=${encodeURIComponent(viewingPath)}`, {
          headers: csrf ? { "X-CS-CSRF": csrf } : undefined,
          signal: controller.signal,
        });

        if (!response.ok || !response.body) {
          throw new Error("Could not tail log");
        }

        const reader = response.body.getReader();

        try {
          while (active) {
            const { done, value } = await reader.read();

            if (done) {
              break;
            }

            const text = partial + decoder.decode(value, { stream: true });
            const lines = text.split("\n");

            partial = lines.pop() ?? "";
            if (lines.length > 0) {
              setLogLines((current) => {
                const next = [...current, ...lines.map((line) => line.replace(/\r$/, ""))];

                return next.length > MAX_LINES ? next.slice(-MAX_LINES) : next;
              });
            }
          }
        } finally {
          reader.releaseLock();
        }
      } catch (error) {
        if (active && !controller.signal.aborted) {
          setTailError(error instanceof Error ? error.message : "Log tail failed");
        }
      }
    })();

    return () => {
      active = false;
      controller.abort();
    };
  }, [csrf, viewingPath]);

  const sortedFiles = useMemo(() => {
    const direction = sortDirection === "asc" ? 1 : -1;

    return [...files].sort((left, right) => {
      if (sortKey === "path") {
        return direction * left.path.localeCompare(right.path);
      }
      if (sortKey === "size") {
        return direction * (left.size - right.size);
      }

      return direction * (left.modified - right.modified);
    });
  }, [files, sortDirection, sortKey]);

  const viewingFile = viewingPath ? files.find((file) => file.path === viewingPath) : undefined;

  function openLog(path: string) {
    setViewingPath(path);
    onPathChange(path);
  }

  function toggleSort(nextKey: SortKey) {
    if (sortKey === nextKey) {
      setSortDirection((current) => (current === "asc" ? "desc" : "asc"));
      return;
    }

    setSortKey(nextKey);
    setSortDirection(nextKey === "modified" ? "desc" : "asc");
  }

  async function handleDownloadAll() {
    setDownloadingAll(true);
    setNotice(null);
    setProgress("");

    try {
      const zip = new JSZip();

      for (let index = 0; index < files.length; index += 1) {
        const file = files[index];
        const response = await fetch(buildLogDownloadUrl(file.path, csrf));

        if (!response.ok) {
          throw new Error(`Could not download ${file.path}`);
        }

        setProgress(`Downloading ${index + 1}/${files.length}: ${file.path}`);
        zip.file(file.path.replace(/^\.?\//, ""), await response.arrayBuffer());
      }

      setProgress("Creating zip...");
      const blob = await zip.generateAsync({ type: "blob" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");

      link.href = url;
      link.download = `central-scrutinizer-logs-${new Date().toISOString().replace(/[:.]/g, "-")}.zip`;
      link.click();
      URL.revokeObjectURL(url);
      setNotice(`Downloaded ${files.length} log file${files.length === 1 ? "" : "s"}.`);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Bulk download failed");
    } finally {
      setDownloadingAll(false);
      setProgress("");
    }
  }

  async function handleDownloadSingle(path: string) {
    setDownloadingPath(path);
    setNotice(null);

    try {
      const response = await fetch(buildLogDownloadUrl(path, csrf));

      if (!response.ok) {
        throw new Error(`Could not download ${path}`);
      }

      const blob = await response.blob();
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");

      link.href = url;
      link.download = path.replace(/\//g, "_");
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Download failed");
    } finally {
      setDownloadingPath(null);
    }
  }

  if (viewingFile) {
    return (
      <div className="space-y-5">
        <button
          className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
          onClick={() => {
            setViewingPath(undefined);
            onPathChange(undefined);
          }}
          type="button"
        >
          <span aria-hidden="true">←</span>
          Back to logs
        </button>
        <div className="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
          <div className="min-w-0">
            <h2 className="text-lg font-semibold">Log Tail</h2>
            <p className="mt-1 break-words font-mono text-sm text-[var(--muted)]">{viewingFile.path}</p>
          </div>
          <div className="flex flex-wrap items-center gap-3">
            <label className="flex items-center gap-2 text-sm text-[var(--muted)]">
              <input checked={autoScroll} onChange={(event) => setAutoScroll(event.target.checked)} type="checkbox" />
              Auto-scroll
            </label>
            <button
              className="rounded-full border border-[var(--border)] px-4 py-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={() => {
                setLogLines([]);
              }}
              type="button"
            >
              Clear View
            </button>
          </div>
        </div>
        {tailError ? (
          <div className="rounded-lg border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
            {tailError}
          </div>
        ) : null}
        <div
          className="min-h-[28rem] rounded-2xl border border-[var(--border)] bg-black/40 p-4 font-mono text-xs leading-6 text-[var(--text)]"
          ref={tailContainerRef}
        >
          <div className="max-h-[60vh] overflow-auto pr-2">
            {logLines.length === 0 && !tailError ? (
              <div className="italic text-[var(--muted)]">Waiting for log output...</div>
            ) : (
              logLines.map((line, index) => (
                <div className="whitespace-pre-wrap break-all" key={`${index}-${line}`}>
                  {line}
                </div>
              ))
            )}
          </div>
        </div>
        <p className="text-xs text-[var(--muted)]">
          {logLines.length} line{logLines.length === 1 ? "" : "s"}
          {logLines.length >= MAX_LINES ? ` (capped at ${MAX_LINES})` : ""}
        </p>
      </div>
    );
  }

  return (
    <div className="space-y-5">
      <button
        className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={onBack}
        type="button"
      >
        <span aria-hidden="true">←</span>
        Back
      </button>
      <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
        <div className="min-w-0">
          <h2 className="text-lg font-semibold">Log Viewer</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">Scan `.userdata` for logs, then tail or download them.</p>
        </div>
        <div className="flex flex-wrap items-center gap-3">
          <button
            className="rounded-full border border-[var(--border)] px-4 py-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={() => {
              setLoading(true);
              setNotice(null);
              if (!csrf) {
                setLoading(false);
                setNotice("Missing session csrf token.");
                return;
              }
              void getLogs(csrf)
                .then((response) => {
                  setFiles(response.files);
                })
                .catch((error) => {
                  setNotice(error instanceof Error ? error.message : "Logs lookup failed");
                })
                .finally(() => {
                  setLoading(false);
                });
            }}
            type="button"
          >
            {loading ? "Scanning..." : "Rescan"}
          </button>
          <button
            className="rounded-full border border-[var(--border)] bg-[var(--accent-soft)] px-4 py-2 text-sm font-semibold text-[var(--accent)] transition hover:border-[var(--accent)]/40"
            disabled={downloadingAll || files.length === 0}
            onClick={() => {
              void handleDownloadAll();
            }}
            type="button"
          >
            {downloadingAll ? "Downloading..." : "Download All as Zip"}
          </button>
        </div>
      </div>
      {notice ? (
        <div className="rounded-lg border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}
      {progress ? <p className="text-sm text-[var(--muted)]">{progress}</p> : null}
      <div className="rounded-2xl border border-[var(--border)] bg-[var(--panel)]">
        <div className="hidden grid-cols-[minmax(0,1fr)_auto_auto_auto] gap-3 border-b border-[var(--border)] px-4 py-3 text-xs font-semibold uppercase tracking-[0.2em] text-[var(--muted)] md:grid">
          <button className="text-left" onClick={() => toggleSort("path")} type="button">
            File
          </button>
          <button onClick={() => toggleSort("size")} type="button">
            Size
          </button>
          <button onClick={() => toggleSort("modified")} type="button">
            Modified
          </button>
          <span className="text-right">Actions</span>
        </div>
        {loading ? (
          <div className="px-4 py-10 text-center text-sm text-[var(--muted)]">Scanning for log files...</div>
        ) : sortedFiles.length === 0 ? (
          <div className="px-4 py-10 text-center text-sm text-[var(--muted)]">No log files found.</div>
        ) : (
          <>
            <div className="divide-y divide-[var(--border)] md:hidden">
              {sortedFiles.map((file) => (
                <article className="space-y-4 px-4 py-4" key={file.path}>
                  <div className="space-y-1.5">
                    <p className="text-[10px] font-semibold uppercase tracking-[0.2em] text-[var(--muted)]">Path</p>
                    <p className="break-words font-mono text-sm leading-5 text-[var(--text)]">{file.path}</p>
                  </div>
                  <dl className="grid grid-cols-2 gap-3 text-sm">
                    <div className="min-w-0">
                      <dt className="text-[10px] font-semibold uppercase tracking-[0.2em] text-[var(--muted)]">Size</dt>
                      <dd className="mt-1 text-[var(--muted)]">{formatSize(file.size)}</dd>
                    </div>
                    <div className="min-w-0">
                      <dt className="text-[10px] font-semibold uppercase tracking-[0.2em] text-[var(--muted)]">Last Updated</dt>
                      <dd className="mt-1 text-[var(--muted)]">{formatDate(file.modified)}</dd>
                    </div>
                  </dl>
                  <div className="flex flex-wrap gap-2">
                    <button
                      className="flex-1 rounded-full border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
                      onClick={() => {
                        openLog(file.path);
                      }}
                      type="button"
                    >
                      Open Tail
                    </button>
                    <button
                      aria-label={`Download ${file.path}`}
                      className="flex-1 rounded-full border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
                      disabled={downloadingPath === file.path}
                      onClick={() => {
                        void handleDownloadSingle(file.path);
                      }}
                      type="button"
                    >
                      {downloadingPath === file.path ? "..." : "Download"}
                    </button>
                  </div>
                </article>
              ))}
            </div>
            <div className="hidden md:block">
              {sortedFiles.map((file) => (
                <div
                  className="grid grid-cols-[minmax(0,1fr)_auto_auto_auto] gap-3 border-b border-[var(--border)] px-4 py-3 text-sm last:border-b-0"
                  key={file.path}
                >
                  <button
                    className="min-w-0 break-words text-left font-mono text-[var(--text)] transition hover:text-[var(--accent)]"
                    onClick={() => {
                      openLog(file.path);
                    }}
                    type="button"
                  >
                    {file.path}
                  </button>
                  <span className="text-[var(--muted)]">{formatSize(file.size)}</span>
                  <span className="text-[var(--muted)]">{formatDate(file.modified)}</span>
                  <div className="flex items-center justify-end gap-2">
                    <button
                      aria-label={`Download ${file.path}`}
                      className="rounded-full border border-[var(--border)] px-3 py-1 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
                      disabled={downloadingPath === file.path}
                      onClick={() => {
                        void handleDownloadSingle(file.path);
                      }}
                      type="button"
                    >
                      {downloadingPath === file.path ? "..." : "Download"}
                    </button>
                  </div>
                </div>
              ))}
            </div>
          </>
        )}
      </div>
    </div>
  );
}
