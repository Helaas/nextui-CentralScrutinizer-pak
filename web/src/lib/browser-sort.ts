import type { BrowserSortColumn, BrowserSortState } from "./types";

export const DEFAULT_BROWSER_SORT: BrowserSortState = { column: "name", direction: "asc" };

export function nextBrowserSort(current: BrowserSortState, column: BrowserSortColumn): BrowserSortState {
  if (current.column === column) {
    return { column, direction: current.direction === "asc" ? "desc" : "asc" };
  }

  return { column, direction: "asc" };
}
