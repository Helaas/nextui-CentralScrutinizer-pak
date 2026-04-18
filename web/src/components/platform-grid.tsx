import type { PlatformGroup } from "../lib/types";
import { createPlatformDisplayNames, flattenPlatformGroups } from "../lib/platform-display";
import { PlatformCard } from "./platform-card";

export function PlatformGrid({
  groups,
  onSelect,
}: {
  groups: PlatformGroup[];
  onSelect: (tag: string) => void;
}) {
  const displayNames = createPlatformDisplayNames(flattenPlatformGroups(groups));

  return (
    <div className="space-y-8">
      {groups.map((group) => (
        <section key={group.name}>
          <h2 className="mb-3 text-sm font-semibold uppercase tracking-[0.22em] text-[var(--muted)]">
            {group.name}
          </h2>
          <div className="grid grid-cols-1 gap-3 lg:grid-cols-3">
            {group.platforms.map((platform) => (
              <PlatformCard
                key={platform.tag}
                displayName={displayNames.get(platform.tag) ?? platform.name}
                onSelect={onSelect}
                platform={platform}
              />
            ))}
          </div>
        </section>
      ))}
    </div>
  );
}
