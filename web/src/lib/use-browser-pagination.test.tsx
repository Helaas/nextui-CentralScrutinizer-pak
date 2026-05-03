import { act, cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { BrowserEntry, BrowserResponse, BrowserScope } from "./types";
import { useBrowserPagination } from "./use-browser-pagination";

const mockApi = vi.hoisted(() => ({
  getBrowser: vi.fn(),
}));

vi.mock("./api", () => mockApi);

function deferred<T>() {
  let resolve: (value: T) => void = () => {};
  let reject: (error: unknown) => void = () => {};
  const promise = new Promise<T>((promiseResolve, promiseReject) => {
    resolve = promiseResolve;
    reject = promiseReject;
  });

  return { promise, reject, resolve };
}

function entry(name: string): BrowserEntry {
  return {
    name,
    path: name,
    type: "file",
    size: 1,
    modified: 1,
    status: "",
    thumbnailPath: "",
  };
}

function browserResponse(entries: BrowserEntry[], totalCount = entries.length, offset = 0): BrowserResponse {
  return {
    scope: "files",
    title: "Files",
    rootPath: "SD Card",
    path: "Screenshots",
    breadcrumbs: [],
    entries,
    totalCount,
    offset,
    truncated: false,
  };
}

function Harness({
  csrf = "csrf-token",
  enabled = true,
  scope = "files",
}: {
  csrf?: string | null;
  enabled?: boolean;
  scope?: BrowserScope | null;
}) {
  const browser = useBrowserPagination({
    scope,
    path: "Screenshots",
    search: "",
    csrf,
    enabled,
  });

  return (
    <div>
      <div data-testid="entries">{browser.entries.map((item) => item.name).join(",")}</div>
      <div data-testid="loading-more">{String(browser.isLoadingMore)}</div>
      <button onClick={browser.loadMore} type="button">
        Load more
      </button>
      <button
        onClick={() => {
          void browser.refresh();
        }}
        type="button"
      >
        Refresh
      </button>
    </div>
  );
}

describe("useBrowserPagination", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("clears load-more state when refresh supersedes an in-flight page", async () => {
    const loadMore = deferred<BrowserResponse>();

    mockApi.getBrowser
      .mockResolvedValueOnce(browserResponse([entry("first.png")], 2))
      .mockReturnValueOnce(loadMore.promise)
      .mockResolvedValueOnce(browserResponse([entry("fresh.png")], 1));

    render(<Harness />);

    await waitFor(() => {
      expect(screen.getByTestId("entries").textContent).toBe("first.png");
    });

    fireEvent.click(screen.getByRole("button", { name: "Load more" }));
    await waitFor(() => {
      expect(screen.getByTestId("loading-more").textContent).toBe("true");
    });

    fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
    await waitFor(() => {
      expect(screen.getByTestId("loading-more").textContent).toBe("false");
      expect(screen.getByTestId("entries").textContent).toBe("fresh.png");
    });

    await act(async () => {
      loadMore.resolve(browserResponse([entry("stale.png")], 2, 1));
      await loadMore.promise;
    });

    expect(screen.getByTestId("loading-more").textContent).toBe("false");
    expect(screen.getByTestId("entries").textContent).toBe("fresh.png");
  });

  it("invalidates in-flight load-more responses when disabled", async () => {
    const loadMore = deferred<BrowserResponse>();

    mockApi.getBrowser
      .mockResolvedValueOnce(browserResponse([entry("first.png")], 2))
      .mockReturnValueOnce(loadMore.promise);
    const { rerender } = render(<Harness />);

    await waitFor(() => {
      expect(screen.getByTestId("entries").textContent).toBe("first.png");
    });

    fireEvent.click(screen.getByRole("button", { name: "Load more" }));
    await waitFor(() => {
      expect(screen.getByTestId("loading-more").textContent).toBe("true");
    });

    rerender(<Harness enabled={false} />);
    await waitFor(() => {
      expect(screen.getByTestId("loading-more").textContent).toBe("false");
      expect(screen.getByTestId("entries").textContent).toBe("");
    });

    await act(async () => {
      loadMore.resolve(browserResponse([entry("stale.png")], 2, 1));
      await loadMore.promise;
    });

    expect(screen.getByTestId("entries").textContent).toBe("");
  });
});
