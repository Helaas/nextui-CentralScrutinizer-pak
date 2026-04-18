function ToolCard({
  title,
  description,
  disabled = false,
  onClick,
}: {
  title: string;
  description: string;
  disabled?: boolean;
  onClick: () => void;
}) {
  return (
    <button
      aria-disabled={disabled}
      className={`group flex flex-col gap-3 rounded-2xl border p-5 text-left transition ${
        disabled
          ? "cursor-not-allowed border-[var(--border)] bg-[var(--panel)]/60 text-[var(--muted)]"
          : "border-[var(--border)] bg-[var(--panel)] hover:border-[var(--accent)]/50"
      }`}
      disabled={disabled}
      onClick={onClick}
      type="button"
    >
      <div>
        <p className={`font-semibold ${disabled ? "" : "group-hover:text-[var(--accent)]"}`}>{title}</p>
        <p className="mt-1 text-sm text-[var(--muted)]">{description}</p>
      </div>
      <span className="text-xs uppercase tracking-[0.2em] text-[var(--muted)]">
        {disabled ? "Enable on handheld" : "Open tool"}
      </span>
    </button>
  );
}

export function ToolsView({
  onOpenCollections,
  terminalEnabled,
  onOpenScreenshots,
  onOpenFileBrowser,
  onOpenLogs,
  onOpenTerminal,
}: {
  onOpenCollections: () => void;
  terminalEnabled: boolean;
  onOpenScreenshots: () => void;
  onOpenFileBrowser: () => void;
  onOpenLogs: () => void;
  onOpenTerminal: () => void;
}) {
  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-semibold">Tools</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">
            Device utilities for files, logs, and direct shell access.
          </p>
        </div>
      </div>
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-3">
        <ToolCard
          description="Browse the SD card filesystem and manage folders from the tools workspace."
          onClick={onOpenFileBrowser}
          title="File Browser"
        />
        <ToolCard
          description="Create collections, reorder ROM paths, and manage collection artwork."
          onClick={onOpenCollections}
          title="Collections"
        />
        <ToolCard
          description="Browse screenshots, preview them, delete them, or download a zip archive."
          onClick={onOpenScreenshots}
          title="Screenshots"
        />
        <ToolCard
          description="Scan .userdata for logs, download them, and follow output live."
          onClick={onOpenLogs}
          title="Log Viewer"
        />
        <ToolCard
          description={
            terminalEnabled
              ? "Open a PTY-backed shell in the browser after acknowledging the safety warning."
              : "Terminal access is disabled on the handheld. Enable it from the device settings screen."
          }
          disabled={!terminalEnabled}
          onClick={onOpenTerminal}
          title="Terminal"
        />
      </div>
    </div>
  );
}
