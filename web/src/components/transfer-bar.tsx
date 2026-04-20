export function TransferBar({
  active,
  cancellable = false,
  label,
  onCancel,
  progress,
}: {
  active: boolean;
  cancellable?: boolean;
  label: string;
  onCancel?: () => void;
  progress: number;
}) {
  if (!active) {
    return null;
  }

  return (
    <div className="rounded-2xl border border-[var(--border)] bg-[var(--panel)] p-4">
      <div className="mb-2 flex items-center justify-between text-sm">
        <div className="flex items-center gap-3">
          <span>{label}</span>
          {cancellable && onCancel ? (
            <button
              className="rounded-full border border-[var(--border)] px-3 py-1 text-xs font-semibold uppercase tracking-[0.12em] text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={onCancel}
              type="button"
            >
              Cancel Upload
            </button>
          ) : null}
        </div>
        <span className="text-[var(--muted)]">{progress}%</span>
      </div>
      <div className="h-2 rounded-full bg-black/20">
        <div className="h-2 rounded-full bg-[var(--accent)] transition-[width]" style={{ width: `${progress}%` }} />
      </div>
    </div>
  );
}
