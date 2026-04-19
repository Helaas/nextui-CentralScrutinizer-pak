import type { BrowserScope, PlatformGroup, PlatformResource, PlatformSummary } from "./types";

const platformResourceOrder: PlatformResource[] = ["roms", "saves", "states", "bios", "overlays", "cheats"];

const platformCardLabels: Record<PlatformResource, string> = {
  roms: "ROMs",
  saves: "Saves",
  states: "States",
  bios: "BIOS",
  overlays: "Overlays",
  cheats: "Cheats",
};

const platformDescriptionLabels: Record<PlatformResource, string> = {
  roms: "ROMs",
  saves: "saves",
  states: "states",
  bios: "BIOS files",
  overlays: "overlays",
  cheats: "cheats",
};

function normalizePlatformName(name: string): string {
  return name.trim().toLowerCase();
}

function matchesPlatformSearch(platform: PlatformSummary, query: string): boolean {
  const searchableText = `${platform.name} ${platform.tag} ${platform.name} (${platform.tag})`.toLowerCase();

  return searchableText.includes(query);
}

export function getSupportedPlatformResources(platform: PlatformSummary): PlatformResource[] {
  return platformResourceOrder.filter((resource) => platform.supportedResources[resource]);
}

export function platformSupportsResource(platform: PlatformSummary, resource: PlatformResource): boolean {
  return platform.supportedResources[resource];
}

export function platformSupportsBrowserScope(
  platform: PlatformSummary,
  scope: Exclude<BrowserScope, "files">,
): boolean {
  return platformSupportsResource(platform, scope);
}

function platformHasVisibleContent(platform: PlatformSummary): boolean {
  return getSupportedPlatformResources(platform).some((resource) => platform.counts[resource] > 0);
}

export function formatPlatformCardSummary(platform: PlatformSummary): string {
  return getSupportedPlatformResources(platform)
    .map((resource) => `${platform.counts[resource]} ${platformCardLabels[resource]}`)
    .join(" · ");
}

export function formatPlatformDescription(platform: PlatformSummary): string {
  return getSupportedPlatformResources(platform)
    .map((resource) => `${platform.counts[resource]} ${platformDescriptionLabels[resource]}`)
    .join(", ");
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
