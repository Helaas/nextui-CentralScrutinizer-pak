import type { AppDestination } from "../lib/navigation";
import type { TransferState } from "../lib/types";
import { PrimaryNav } from "./primary-nav";

export function TopBar({
  activeDestination,
  compact = false,
  onDestinationChange,
  onDisconnect,
  onSearchChange,
  searchPlaceholder,
  searchValue,
  showSearch,
  transfer,
}: {
  activeDestination: AppDestination;
  compact?: boolean;
  onDestinationChange: (destination: AppDestination) => void;
  onDisconnect: () => void;
  onSearchChange: (value: string) => void;
  searchPlaceholder: string;
  searchValue: string;
  showSearch: boolean;
  transfer: TransferState;
}) {
  const headerClass = compact
    ? "rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-4 py-3 shadow-[var(--shadow)] md:rounded-[28px] md:px-5 md:py-4"
    : "rounded-[28px] border border-[var(--border)] bg-[var(--panel)] px-5 py-4 shadow-[var(--shadow)]";
  const logoClass = compact ? "h-14 w-auto md:h-20" : "h-20 w-auto";
  const eyebrowClass = compact
    ? "hidden text-xs font-semibold uppercase tracking-[0.2em] text-[var(--muted)] md:block"
    : "text-xs font-semibold uppercase tracking-[0.2em] text-[var(--muted)]";
  const titleClass = compact ? "text-base font-bold leading-tight md:text-lg" : "text-lg font-bold";
  const secondaryRowClass = compact
    ? "hidden flex-col gap-3 md:flex lg:flex-row lg:items-center lg:justify-between"
    : "flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between";
  const disconnectClass = compact
    ? "rounded-full border border-[var(--border)] px-3 py-2 text-xs font-medium text-[var(--muted)] transition hover:text-[var(--text)] md:px-4 md:text-sm"
    : "rounded-full border border-[var(--border)] px-4 py-2 text-sm font-medium text-[var(--muted)] transition hover:text-[var(--text)]";

  return (
    <header className={headerClass}>
      <div className="flex flex-col gap-4">
        <div className="flex items-center justify-between gap-4">
          <div className="flex items-center gap-3">
            <img alt="The Central Scrutinizer" className={logoClass} src="/logo.png" />
            <div>
              <p className={eyebrowClass}>Central Scrutinizer</p>
              <p className={titleClass}>Device Library Workspace</p>
            </div>
          </div>
          <button className={disconnectClass} onClick={onDisconnect} type="button">
            Disconnect
          </button>
        </div>
        <div className={secondaryRowClass}>
          <PrimaryNav active={activeDestination} onChange={onDestinationChange} />
          <div className="flex items-center gap-3">
            {showSearch ? (
              <label className="sr-only" htmlFor="shell-search">
                Search
              </label>
            ) : null}
            {showSearch ? (
              <input
                aria-label="Search"
                className="h-10 w-full rounded-full border border-[var(--border)] bg-[var(--bg)] px-4 text-sm text-[var(--text)] outline-none focus:border-[var(--accent)] lg:w-72"
                id="shell-search"
                onChange={(event) => {
                  onSearchChange(event.target.value);
                }}
                placeholder={searchPlaceholder}
                value={searchValue}
              />
            ) : null}
            <div className="min-w-[10rem] rounded-full bg-[var(--bg)] px-4 py-2 text-xs text-[var(--muted)]">
              {transfer.active ? `${transfer.label} · ${transfer.progress}%` : "No active transfers"}
            </div>
          </div>
        </div>
      </div>
    </header>
  );
}
