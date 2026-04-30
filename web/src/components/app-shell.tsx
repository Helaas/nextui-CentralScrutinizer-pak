import type { ReactNode } from "react";

import type { AppDestination } from "../lib/navigation";
import type { TransferState } from "../lib/types";
import { MobileNav } from "./mobile-nav";
import { PageHeader } from "./page-header";
import { TopBar } from "./top-bar";

export function AppShell({
  actions,
  children,
  description,
  destination,
  fillViewport = false,
  onDestinationChange,
  onDisconnect,
  onSearchChange,
  searchPlaceholder,
  searchValue,
  showPageHeader = true,
  showSearch,
  title,
  transfer,
}: {
  actions?: ReactNode;
  children: ReactNode;
  description: string;
  destination: AppDestination;
  fillViewport?: boolean;
  onDestinationChange: (destination: AppDestination) => void;
  onDisconnect: () => void;
  onSearchChange: (value: string) => void;
  searchPlaceholder: string;
  searchValue: string;
  showPageHeader?: boolean;
  showSearch: boolean;
  title: string;
  transfer: TransferState;
}) {
  const mainClass = fillViewport
    ? "h-[100dvh] overflow-hidden px-4 py-4 text-[var(--text)] md:px-6 md:py-6"
    : "min-h-screen px-4 py-4 text-[var(--text)] md:px-6 md:py-6";
  const sectionClass = fillViewport
    ? "mx-auto flex h-full max-w-7xl flex-col gap-5 overflow-hidden pb-24 md:pb-0"
    : "mx-auto flex max-w-7xl flex-col gap-5 pb-24 md:pb-6";
  const contentClass = fillViewport
    ? "min-h-0 flex-1 overflow-hidden rounded-[28px] border border-[var(--border)] bg-[var(--bg-deep)] px-5 py-5"
    : "rounded-[28px] border border-[var(--border)] bg-[var(--bg-deep)] px-5 py-5";

  return (
    <main className={mainClass}>
      <section className={sectionClass}>
        <div className={fillViewport ? "shrink-0" : undefined}>
          <TopBar
            activeDestination={destination}
            compact={fillViewport}
            onDestinationChange={onDestinationChange}
            onDisconnect={onDisconnect}
            onSearchChange={onSearchChange}
            searchPlaceholder={searchPlaceholder}
            searchValue={searchValue}
            showSearch={showSearch}
            transfer={transfer}
          />
        </div>
        {showPageHeader ? (
          <div className={fillViewport ? "shrink-0" : undefined}>
            <PageHeader actions={actions} description={description} title={title} />
          </div>
        ) : null}
        <div className={contentClass}>{children}</div>
      </section>
      <MobileNav active={destination} onChange={onDestinationChange} />
    </main>
  );
}
