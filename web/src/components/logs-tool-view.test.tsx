import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { LogsToolView } from "./logs-tool-view";

const mockApi = vi.hoisted(() => ({
  buildLogDownloadUrl: vi.fn((path: string) => `/api/logs/download?path=${encodeURIComponent(path)}`),
  getLogs: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

describe("LogsToolView", () => {
  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
    vi.clearAllMocks();
  });

  it("renders stacked metadata and explicit actions for log files", async () => {
    mockApi.getLogs.mockResolvedValue({
      root: ".userdata",
      files: [
        {
          path: "shared/minui/settings.txt",
          size: 827,
          modified: 1_713_424_899,
        },
      ],
    });

    render(<LogsToolView csrf="csrf-token" onBack={vi.fn()} onPathChange={vi.fn()} />);

    expect(await screen.findByText("Path")).toBeTruthy();
    expect(screen.getByText("Last Updated")).toBeTruthy();
    expect(screen.getAllByText("shared/minui/settings.txt")).toHaveLength(2);
    expect(screen.getByRole("button", { name: "Open Tail" })).toBeTruthy();
    expect(screen.getAllByRole("button", { name: "Download shared/minui/settings.txt" })).toHaveLength(2);
  });

  it("opens the selected log from the explicit mobile action", async () => {
    const onPathChange = vi.fn();

    mockApi.getLogs.mockResolvedValue({
      root: ".userdata",
      files: [
        {
          path: "tg5040/logs/central-scrutinizer.log",
          size: 837,
          modified: 1_713_424_899,
        },
      ],
    });
    vi.spyOn(globalThis, "fetch").mockResolvedValue({
      ok: false,
      body: null,
    } as Response);

    render(<LogsToolView csrf="csrf-token" onBack={vi.fn()} onPathChange={onPathChange} />);

    fireEvent.click(await screen.findByRole("button", { name: "Open Tail" }));

    await waitFor(() => {
      expect(onPathChange).toHaveBeenCalledWith("tg5040/logs/central-scrutinizer.log");
    });
  });
});
