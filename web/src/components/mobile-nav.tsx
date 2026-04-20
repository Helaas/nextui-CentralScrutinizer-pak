import type { AppDestination } from "../lib/navigation";

const items: Array<{ key: AppDestination; label: string }> = [
  { key: "library", label: "Library" },
  { key: "tools", label: "Tools" },
];

export function MobileNav({
  active,
  onChange,
}: {
  active: AppDestination;
  onChange: (destination: AppDestination) => void;
}) {
  return (
    <nav
      aria-label="Mobile"
      className="fixed inset-x-0 bottom-0 z-40 border-t border-[var(--border)] bg-[var(--bg-deep)]/95 px-4 py-3 md:hidden"
    >
      <div className="grid grid-cols-2 gap-2">
        {items.map((item) => (
          <button
            key={item.key}
            aria-current={active === item.key ? "page" : undefined}
            className={`rounded-xl px-3 py-2 text-sm font-semibold ${
              active === item.key ? "bg-[var(--accent)] text-white" : "text-[var(--muted)]"
            }`}
            onClick={() => {
              onChange(item.key);
            }}
            type="button"
          >
            {item.label}
          </button>
        ))}
      </div>
    </nav>
  );
}
