import type { Breadcrumb } from "../lib/types";

export function Breadcrumbs({
  ariaLabel = "Current path",
  items,
  onSelect,
  rootLabel = "Root",
}: {
  ariaLabel?: string;
  items: Breadcrumb[];
  onSelect: (path?: string) => void;
  rootLabel?: string;
}) {
  return (
    <nav aria-label={ariaLabel} className="flex flex-wrap items-center gap-2 text-sm text-[var(--muted)]">
      <button
        className="transition hover:text-white"
        onClick={() => {
          onSelect(undefined);
        }}
        type="button"
      >
        {rootLabel}
      </button>
      {items.map((item) => (
        <span key={item.path} className="flex items-center gap-2">
          <span>/</span>
          <button
            className="transition hover:text-white"
            onClick={() => {
              onSelect(item.path);
            }}
            type="button"
          >
            {item.label}
          </button>
        </span>
      ))}
    </nav>
  );
}
