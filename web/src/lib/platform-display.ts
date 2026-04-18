import type { PlatformGroup, PlatformSummary } from "./types";

function normalizePlatformName(name: string): string {
  return name.trim().toLowerCase();
}

function matchesPlatformSearch(platform: PlatformSummary, query: string): boolean {
  const searchableText = `${platform.name} ${platform.tag} ${platform.name} (${platform.tag})`.toLowerCase();

  return searchableText.includes(query);
}

function platformHasVisibleContent(platform: PlatformSummary): boolean {
  return (
    platform.counts.roms > 0 ||
    platform.counts.saves > 0 ||
    platform.counts.states > 0 ||
    platform.counts.bios > 0 ||
    platform.counts.overlays > 0 ||
    platform.counts.cheats > 0
  );
}

export function flattenPlatformGroups(groups: PlatformGroup[]): PlatformSummary[] {
  return groups.flatMap((group) => group.platforms);
}

export function filterPlatformGroups(
  groups: PlatformGroup[],
  search: string,
  showEmptyPlatforms: boolean,
): PlatformGroup[] {
  const query = search.trim().toLowerCase();

  return groups
    .map((group) => ({
      ...group,
      platforms: group.platforms.filter((platform) => {
        if (!showEmptyPlatforms && !platformHasVisibleContent(platform)) {
          return false;
        }
        if (!query) {
          return true;
        }

        return matchesPlatformSearch(platform, query);
      }),
    }))
    .filter((group) => group.platforms.length > 0);
}

export function createPlatformDisplayNames(platforms: PlatformSummary[]): Map<string, string> {
  const nameCounts = new Map<string, number>();

  for (const platform of platforms) {
    const key = normalizePlatformName(platform.name);

    nameCounts.set(key, (nameCounts.get(key) ?? 0) + 1);
  }

  return new Map(
    platforms.map((platform) => {
      const hasDuplicateName = (nameCounts.get(normalizePlatformName(platform.name)) ?? 0) > 1;

      return [platform.tag, hasDuplicateName ? `${platform.name} (${platform.tag})` : platform.name];
    }),
  );
}
