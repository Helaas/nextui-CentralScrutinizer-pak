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
  return (
    <main className="min-h-screen px-4 py-4 text-[var(--text)] md:px-6 md:py-6">
      <section className="mx-auto flex max-w-7xl flex-col gap-5 pb-24 md:pb-6">
        <TopBar
          activeDestination={destination}
          onDestinationChange={onDestinationChange}
          onDisconnect={onDisconnect}
          onSearchChange={onSearchChange}
          searchPlaceholder={searchPlaceholder}
          searchValue={searchValue}
          showSearch={showSearch}
          transfer={transfer}
        />
        {showPageHeader ? <PageHeader actions={actions} description={description} title={title} /> : null}
        <div className="rounded-[28px] border border-[var(--border)] bg-[var(--bg-deep)] px-5 py-5">{children}</div>
      </section>
      <MobileNav active={destination} onChange={onDestinationChange} />
    </main>
  );
}
