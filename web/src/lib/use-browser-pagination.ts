import { useCallback, useEffect, useRef, useState } from "react";

import { getBrowser } from "./api";
import type { BrowserEntry, BrowserResponse, BrowserScope } from "./types";

export const BROWSER_PAGE_SIZE = 512;
export const BROWSER_SEARCH_DEBOUNCE_MS = 300;

export type BrowserMetadata = Omit<BrowserResponse, "entries">;

export type BrowserPaginationParams = {
  scope: BrowserScope | null;
  tag?: string;
  path?: string;
  search: string;
  csrf?: string | null;
  enabled: boolean;
};

export type BrowserPaginationResult = {
  metadata: BrowserMetadata | null;
  entries: BrowserEntry[];
  hasMore: boolean;
  isLoading: boolean;
  isLoadingMore: boolean;
  error: string | null;
  loadMore: () => void;
  refresh: () => Promise<void>;
};

export function useBrowserPagination(params: BrowserPaginationParams): BrowserPaginationResult {
  const { scope, tag, path, search, csrf, enabled } = params;
  const [metadata, setMetadata] = useState<BrowserMetadata | null>(null);
  const [entries, setEntries] = useState<BrowserEntry[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [isLoadingMore, setIsLoadingMore] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const requestSeqRef = useRef(0);
  const debouncedSearchRef = useRef(search);
  const [debouncedSearch, setDebouncedSearch] = useState(search);

  useEffect(() => {
    if (debouncedSearchRef.current === search) {
      return;
    }
    const timer = window.setTimeout(() => {
      debouncedSearchRef.current = search;
      setDebouncedSearch(search);
    }, BROWSER_SEARCH_DEBOUNCE_MS);

    return () => {
      window.clearTimeout(timer);
    };
  }, [search]);

  const fetchPage = useCallback(
    async (offset: number, signal: AbortSignal) => {
      if (!enabled || !scope || !csrf) {
        return null;
      }

      const response = await getBrowser(scope, csrf, tag, path, {
        offset,
        query: debouncedSearch,
        signal,
      });

      return response;
    },
    [enabled, scope, csrf, tag, path, debouncedSearch],
  );

  useEffect(() => {
    if (!enabled || !scope || !csrf) {
      setMetadata(null);
      setEntries([]);
      setError(null);
      setIsLoading(false);
      setIsLoadingMore(false);
      return;
    }

    const controller = new AbortController();
    const seq = ++requestSeqRef.current;

    setIsLoading(true);
    setError(null);
    void (async () => {
      try {
        const response = await fetchPage(0, controller.signal);

        if (seq !== requestSeqRef.current || !response) {
          return;
        }

        const { entries: nextEntries, ...rest } = response;
        setMetadata(rest);
        setEntries(nextEntries);
      } catch (caught) {
        if (seq !== requestSeqRef.current || controller.signal.aborted) {
          return;
        }
        setError(caught instanceof Error ? caught.message : "Browser lookup failed");
        setMetadata(null);
        setEntries([]);
      } finally {
        if (seq === requestSeqRef.current) {
          setIsLoading(false);
        }
      }
    })();

    return () => {
      controller.abort();
    };
  }, [enabled, scope, csrf, tag, path, debouncedSearch, fetchPage]);

  const loadMore = useCallback(() => {
    if (!metadata || isLoadingMore) {
      return;
    }
    if (entries.length >= metadata.totalCount) {
      return;
    }

    const controller = new AbortController();
    const seq = requestSeqRef.current;

    setIsLoadingMore(true);
    void (async () => {
      try {
        const response = await fetchPage(entries.length, controller.signal);

        if (seq !== requestSeqRef.current || !response) {
          return;
        }

        const { entries: nextEntries, ...rest } = response;
        setMetadata(rest);
        setEntries((current) => [...current, ...nextEntries]);
      } catch (caught) {
        if (seq !== requestSeqRef.current || controller.signal.aborted) {
          return;
        }
        setError(caught instanceof Error ? caught.message : "Browser lookup failed");
      } finally {
        if (seq === requestSeqRef.current) {
          setIsLoadingMore(false);
        }
      }
    })();
  }, [entries.length, fetchPage, isLoadingMore, metadata]);

  const refresh = useCallback(async () => {
    if (!enabled || !scope || !csrf) {
      return;
    }

    const controller = new AbortController();
    const seq = ++requestSeqRef.current;

    setIsLoading(true);
    setError(null);
    try {
      const response = await fetchPage(0, controller.signal);

      if (seq !== requestSeqRef.current || !response) {
        return;
      }

      const { entries: nextEntries, ...rest } = response;
      setMetadata(rest);
      setEntries(nextEntries);
    } catch (caught) {
      if (seq !== requestSeqRef.current || controller.signal.aborted) {
        return;
      }
      setError(caught instanceof Error ? caught.message : "Browser lookup failed");
    } finally {
      if (seq === requestSeqRef.current) {
        setIsLoading(false);
      }
    }
  }, [enabled, scope, csrf, fetchPage]);

  const hasMore = metadata !== null && entries.length < metadata.totalCount;

  return {
    metadata,
    entries,
    hasMore,
    isLoading,
    isLoadingMore,
    error,
    loadMore,
    refresh,
  };
}
