import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { ScreenshotsToolView } from "./screenshots-tool-view";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn((scope: string, path: string) => `/api/download?scope=${scope}&path=${encodeURIComponent(path)}`),
  deleteItem: vi.fn(),
  getBrowser: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

describe("ScreenshotsToolView", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("uses consistent button treatments for the bulk and per-card actions", async () => {
    mockApi.getBrowser.mockResolvedValue({
      scope: "files",
      title: "Screenshots",
      rootPath: "Screenshots",
      path: "Screenshots",
      breadcrumbs: [],
      truncated: false,
      entries: [
        {
          name: "capture-01.png",
          path: "Screenshots/capture-01.png",
          type: "file",
          size: 1024,
          modified: 1_713_424_899,
          status: "",
          thumbnailPath: "",
        },
      ],
    });

    render(<ScreenshotsToolView csrf="csrf-token" onBack={vi.fn()} />);

    const downloadAll = await screen.findByRole("button", { name: "Download All" });
    const download = screen.getByRole("link", { name: "Download" });
    const deleteButton = screen.getByRole("button", { name: "Delete" });

    expect(downloadAll.className).toContain("text-white");
    expect(downloadAll.className).not.toContain("text-black");
    expect(download.className).toContain("text-sm");
    expect(download.className).toContain("font-medium");
    expect(download.className).toContain("inline-flex");
    expect(download.className).toContain("flex-1");
    expect(download.className).toContain("min-h-11");
    expect(deleteButton.className).toContain("text-sm");
    expect(deleteButton.className).toContain("font-medium");
    expect(deleteButton.className).toContain("inline-flex");
    expect(deleteButton.className).toContain("flex-1");
    expect(deleteButton.className).toContain("min-h-11");
  });

  it("renders a back button for the selected tools view", () => {
    const onBack = vi.fn();

    mockApi.getBrowser.mockResolvedValue({
      scope: "files",
      title: "Screenshots",
      rootPath: "Screenshots",
      path: "Screenshots",
      breadcrumbs: [],
      truncated: false,
      entries: [],
    });

    render(<ScreenshotsToolView csrf="csrf-token" onBack={onBack} />);

    screen.getByRole("button", { name: "Back" }).click();

    expect(onBack).toHaveBeenCalledTimes(1);
  });
});
