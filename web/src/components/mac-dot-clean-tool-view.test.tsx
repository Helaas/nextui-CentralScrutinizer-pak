import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => ({
  deleteItem: vi.fn(),
  getMacDotfiles: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

import { MacDotCleanToolView } from "./mac-dot-clean-tool-view";

describe("MacDotCleanToolView", () => {
  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
    vi.clearAllMocks();
  });

  it("loads and renders cleanup candidates", async () => {
    mockApi.getMacDotfiles.mockResolvedValue({
      count: 2,
      truncated: false,
      entries: [
        {
          path: ".Spotlight-V100",
          kind: "directory",
          reason: "Top-level macOS metadata",
          size: 0,
          modified: 1_700_000_000,
        },
        {
          path: "Roms/.DS_Store",
          kind: "file",
          reason: "Finder metadata file",
          size: 128,
          modified: 1_700_000_100,
        },
      ],
    });

    render(<MacDotCleanToolView csrf="csrf-token" onBack={vi.fn()} />);

    expect(await screen.findByText(".Spotlight-V100")).toBeTruthy();
    expect(screen.getByText("Roms/.DS_Store")).toBeTruthy();
    expect(screen.getByText("Finder metadata file")).toBeTruthy();
  });

  it("cleans candidates and refreshes the scan", async () => {
    vi.stubGlobal("confirm", vi.fn(() => true));

    mockApi.getMacDotfiles
      .mockResolvedValueOnce({
        count: 2,
        truncated: false,
        entries: [
          {
            path: ".Spotlight-V100",
            kind: "directory",
            reason: "Top-level macOS metadata",
            size: 0,
            modified: 1_700_000_000,
          },
          {
            path: "Roms/.DS_Store",
            kind: "file",
            reason: "Finder metadata file",
            size: 128,
            modified: 1_700_000_100,
          },
        ],
      })
      .mockResolvedValueOnce({
        count: 0,
        truncated: false,
        entries: [],
      });

    render(<MacDotCleanToolView csrf="csrf-token" onBack={vi.fn()} />);

    fireEvent.click(await screen.findByRole("button", { name: "Clean Now" }));

    await waitFor(() => {
      expect(mockApi.deleteItem).toHaveBeenCalledTimes(2);
    });
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(1, { scope: "files", path: ".Spotlight-V100" }, "csrf-token");
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(2, { scope: "files", path: "Roms/.DS_Store" }, "csrf-token");
    expect(await screen.findByText("No macOS transfer artifacts found.")).toBeTruthy();
  });

  it("shows a truncation warning when the scan result is capped", async () => {
    mockApi.getMacDotfiles.mockResolvedValue({
      count: 600,
      truncated: true,
      entries: [
        {
          path: ".Spotlight-V100",
          kind: "directory",
          reason: "Top-level macOS metadata",
          size: 0,
          modified: 1_700_000_000,
        },
      ],
    });

    render(<MacDotCleanToolView csrf="csrf-token" onBack={vi.fn()} />);

    expect(await screen.findByText("Showing 1 of 600 macOS transfer artifacts. Clean Now only removes the listed items; refresh after cleanup to scan for the rest.")).toBeTruthy();
  });
});
