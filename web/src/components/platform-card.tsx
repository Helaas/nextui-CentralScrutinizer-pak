import { useEffect, useState } from "react";

import type { PlatformSummary } from "../lib/types";
import { formatPlatformCardSummary } from "../lib/platform-display";

const FALLBACK_ICON_SRC = "/platforms/UNKNOWN.svg";

function platformIconSrc(icon: string): string {
  return icon ? `/platforms/${icon}.svg` : FALLBACK_ICON_SRC;
}

export function PlatformCard({
  displayName,
  platform,
  onSelect,
}: {
  displayName: string;
  platform: PlatformSummary;
  onSelect: (tag: string) => void;
}) {
  const [iconSrc, setIconSrc] = useState(() => platformIconSrc(platform.icon));

  useEffect(() => {
    setIconSrc(platformIconSrc(platform.icon));
  }, [platform.icon]);

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
        onError={() => {
          if (iconSrc !== FALLBACK_ICON_SRC) {
            setIconSrc(FALLBACK_ICON_SRC);
          }
        }}
        src={iconSrc}
      />
      <div className="min-w-0 flex-1">
        <p className="truncate font-semibold group-hover:text-[var(--accent)]">{displayName}</p>
        <p className="mt-1 text-xs text-[var(--muted)]">{formatPlatformCardSummary(platform)}</p>
        {platform.requiresEmulator && !platform.emulatorInstalled ? (
          <p className="mt-2 text-xs font-semibold uppercase tracking-[0.12em] text-amber-500" title={platform.emulatorWarning ?? undefined}>
            Missing emulator
          </p>
        ) : null}
      </div>
    </button>
  );
}
