import type { PlatformGroup } from "../lib/types";
import { PlatformGrid } from "./platform-grid";

export function DashboardShell({
  groups,
  onSelectPlatform,
  onToggleShowEmpty,
  showEmptyPlatforms,
}: {
  groups: PlatformGroup[];
  onSelectPlatform: (tag: string) => void;
  onToggleShowEmpty: (value: boolean) => void;
  showEmptyPlatforms: boolean;
}) {
  const visibleSystems = groups.reduce((count, group) => count + group.platforms.length, 0);

  return (
    <div className="space-y-8">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-semibold">Platforms</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">Browse library content by platform family.</p>
        </div>
        <div className="flex items-center gap-4">
          <label className="flex items-center gap-2 text-sm text-[var(--muted)]">
            <input
              checked={showEmptyPlatforms}
              className="h-4 w-4 rounded border-[var(--border)] bg-[var(--bg)]"
              onChange={(event) => {
                onToggleShowEmpty(event.target.checked);
              }}
              type="checkbox"
            />
            Show empty consoles
          </label>
          <p className="text-sm text-[var(--muted)]">{visibleSystems} visible systems</p>
        </div>
      </div>
      <PlatformGrid groups={groups} onSelect={onSelectPlatform} />
      <footer className="border-t border-[var(--border)] pt-6 text-center text-xs text-[var(--muted)]/70">
        Platform icons from the{" "}
        <a
          className="underline hover:text-[var(--muted)]"
          href="https://git.libretro.com/libretro-assets/retroarch-assets/-/tree/e11d6708b49a893f392b238effc713c6c7cfadef/xmb/systematic"
          rel="noopener noreferrer"
          target="_blank"
        >
          libretro Systematic theme
        </a>{" "}
        (CC BY 4.0). All trademarks are property of their respective owners.
      </footer>
    </div>
  );
}
