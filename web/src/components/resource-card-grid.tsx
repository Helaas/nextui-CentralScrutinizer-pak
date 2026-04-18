import type { PlatformSummary } from "../lib/types";

type Scope = "roms" | "saves" | "bios" | "overlays" | "cheats";

type Card =
  | { key: Scope; label: string; glyph: string; disabled?: false; count: number }
  | { key: string; label: string; glyph: string; disabled: true };

export function ResourceCardGrid({
  platform,
  onSelect,
}: {
  platform: PlatformSummary;
  onSelect: (scope: Scope) => void;
}) {
  const cards: Card[] = [
    { key: "roms", label: "ROMs", glyph: "ROM", count: platform.counts.roms },
    { key: "saves", label: "Saves", glyph: "SAV", count: platform.counts.saves },
    { key: "bios", label: "BIOS", glyph: "BIO", count: platform.counts.bios },
    { key: "overlays", label: "Overlays", glyph: "OVR", count: platform.counts.overlays },
    { key: "cheats", label: "Cheats", glyph: "CHT", count: platform.counts.cheats },
    { key: "states", label: "Save States", glyph: "STA", disabled: true },
    { key: "guides", label: "Guides", glyph: "GDE", disabled: true },
  ];

  return (
    <div className="grid grid-cols-2 gap-4 sm:grid-cols-3">
      {cards.map((card) => {
        if (card.disabled) {
          return (
            <div
              key={card.key}
              aria-disabled="true"
              className="flex cursor-default flex-col items-center gap-4 rounded-xl border border-[var(--border)] bg-[var(--panel)] p-8 opacity-50"
            >
              <div className="flex h-14 w-14 items-center justify-center rounded-xl bg-[var(--bg)] text-xs font-black uppercase tracking-[0.2em] text-[var(--muted)]">
                {card.glyph}
              </div>
              <span className="text-lg font-semibold">{card.label}</span>
              <span className="-mt-2 text-xs text-[var(--muted)]">Coming soon™</span>
            </div>
          );
        }
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
