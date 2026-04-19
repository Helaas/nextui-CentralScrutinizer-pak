import type { PlatformResource, PlatformSummary } from "../lib/types";

type Card = { key: PlatformResource; label: string; glyph: string; count: number };

export function ResourceCardGrid({
  platform,
  onSelect,
}: {
  platform: PlatformSummary;
  onSelect: (resource: PlatformResource) => void;
}) {
  const cards = ([
    { key: "roms", label: "ROMs", glyph: "ROM", count: platform.counts.roms },
    { key: "saves", label: "Saves", glyph: "SAV", count: platform.counts.saves },
    { key: "states", label: "Save States", glyph: "STA", count: platform.counts.states },
    { key: "bios", label: "BIOS", glyph: "BIO", count: platform.counts.bios },
    { key: "overlays", label: "Overlays", glyph: "OVR", count: platform.counts.overlays },
    { key: "cheats", label: "Cheats", glyph: "CHT", count: platform.counts.cheats },
  ] satisfies Card[]).filter((card) => platform.supportedResources[card.key]);

  return (
    <div className="grid grid-cols-2 gap-4 sm:grid-cols-3">
      {cards.map((card) => {
        return (
          <button
            key={card.key}
            aria-label={card.label}
            className="group flex cursor-pointer flex-col items-center gap-4 rounded-xl border border-[var(--border)] bg-[var(--panel)] p-8 transition hover:border-[var(--accent)]/50 hover:shadow-md hover:shadow-[var(--accent-soft)]"
            onClick={() => {
              onSelect(card.key);
            }}
            type="button"
          >
            <div className="flex h-14 w-14 items-center justify-center rounded-xl bg-[var(--bg)] text-xs font-black uppercase tracking-[0.2em] text-[var(--muted)] group-hover:bg-[var(--accent-soft)] group-hover:text-[var(--accent)]">
              {card.glyph}
            </div>
            <span className="text-lg font-semibold group-hover:text-[var(--accent)]">{card.label}</span>
            <span className="-mt-2 text-xs text-[var(--muted)]">{card.count} items</span>
          </button>
        );
      })}
    </div>
  );
}
