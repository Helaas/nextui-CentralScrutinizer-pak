import type { PlatformSummary } from "../lib/types";
import { formatPlatformCardSummary } from "../lib/platform-display";

export function PlatformCard({
  displayName,
  platform,
  onSelect,
}: {
  displayName: string;
  platform: PlatformSummary;
  onSelect: (tag: string) => void;
}) {
  return (
    <button
      className="group flex w-full cursor-pointer items-center gap-4 rounded-xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5 text-left transition hover:border-[var(--accent)]/50 hover:shadow-md hover:shadow-[var(--accent-soft)]"
      onClick={() => {
        onSelect(platform.tag);
      }}
      type="button"
    >
      <img
        alt=""
        aria-hidden="true"
        className="h-12 w-12 shrink-0 object-contain"
        src={`/platforms/${platform.icon}.svg`}
      />
      <div className="min-w-0 flex-1">
        <p className="truncate font-semibold group-hover:text-[var(--accent)]">{displayName}</p>
        <p className="mt-1 text-xs text-[var(--muted)]">{formatPlatformCardSummary(platform)}</p>
      </div>
    </button>
  );
}
