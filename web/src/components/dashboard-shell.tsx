import type { LibraryEmuFilter, PlatformGroup } from "../lib/types";
import { createPlatformDisplayNames, flattenPlatformGroups } from "../lib/platform-display";
import { PlatformGrid } from "./platform-grid";

export function DashboardShell({
  emuFilter,
  groups,
  onChangeEmuFilter,
  onSelectPlatform,
  onToggleShowEmpty,
  showEmptyPlatforms,
}: {
  emuFilter: LibraryEmuFilter;
  groups: PlatformGroup[];
  onChangeEmuFilter: (value: LibraryEmuFilter) => void;
  onSelectPlatform: (tag: string) => void;
  onToggleShowEmpty: (value: boolean) => void;
  showEmptyPlatforms: boolean;
}) {
  const visiblePlatforms = flattenPlatformGroups(groups);
  const displayNames = createPlatformDisplayNames(visiblePlatforms);
  const missingEmulatorPlatforms =
    emuFilter === "all"
      ? visiblePlatforms.filter((platform) => platform.requiresEmulator && !platform.emulatorInstalled)
      : [];
  const visibleSystems = groups.reduce((count, group) => count + group.platforms.length, 0);
  const missingPlatformNames = missingEmulatorPlatforms
    .slice(0, 3)
    .map((platform) => displayNames.get(platform.tag) ?? platform.name);
  const remainingMissingPlatforms = missingEmulatorPlatforms.length - missingPlatformNames.length;
  const missingEmulatorMessage =
    missingEmulatorPlatforms.length > 0
      ? `${missingEmulatorPlatforms.length} console${missingEmulatorPlatforms.length === 1 ? "" : "s"} ${
          missingEmulatorPlatforms.length === 1 ? "has" : "have"
        } no installed emulator: ${missingPlatformNames.join(", ")}${
          remainingMissingPlatforms > 0 ? `, and ${remainingMissingPlatforms} more` : ""
        }. Install from the Pak Store, or switch to 'Installed emus' to hide them.`
      : null;

  return (
    <div className="space-y-8">
      <div className="flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <h2 className="text-lg font-semibold">Platforms</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">Browse library content by platform family.</p>
        </div>
        <div className="flex flex-wrap items-center gap-x-4 gap-y-2">
          <label className="flex items-center gap-2 text-sm text-[var(--muted)]">
            <span>Console filter</span>
            <select
              aria-label="Console filter"
              className="rounded-md border border-[var(--border)] bg-[var(--bg)] px-2 py-1 text-sm text-[var(--text)]"
              onChange={(event) => {
                onChangeEmuFilter(event.target.value as LibraryEmuFilter);
              }}
              value={emuFilter}
            >
              <option value="all">All supported</option>
              <option value="installed">Installed emus</option>
            </select>
          </label>
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
      {missingEmulatorMessage ? (
        <section className="rounded-[24px] border border-[var(--border)] bg-[var(--panel-alt)] px-5 py-4">
          <div className="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
            <p className="text-sm text-[var(--muted)]">
              <span className="font-semibold text-amber-500">Missing emulator. </span>
              {missingEmulatorMessage}
            </p>
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50"
              onClick={() => {
                onChangeEmuFilter("installed");
              }}
              type="button"
            >
              Show installed only
            </button>
          </div>
        </section>
      ) : null}
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
