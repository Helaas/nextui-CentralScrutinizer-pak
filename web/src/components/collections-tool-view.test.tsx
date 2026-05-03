import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { CollectionsToolView } from "./collections-tool-view";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn(),
  deleteItem: vi.fn(),
  getBrowser: vi.fn(),
  getBrowserAll: vi.fn(),
  readTextFile: vi.fn(),
  uploadFiles: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

describe("CollectionsToolView", () => {
  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("renders a back button for the selected tools view", async () => {
    const onBack = vi.fn();

    mockApi.getBrowserAll.mockResolvedValue({
      scope: "files",
      title: "Collections",
      rootPath: "Collections",
      path: "Collections",
      breadcrumbs: [],
      totalCount: 0,
      offset: 0,
      truncated: false,
      entries: [
        {
          name: "Favorites.txt",
          path: "Collections/Favorites.txt",
          type: "file",
          size: 32,
          modified: 1_713_424_899,
          status: "",
          thumbnailPath: "",
        },
      ],
    });
    mockApi.readTextFile.mockResolvedValue("/Roms/Game Boy Advance (GBA)/Pokemon Emerald.gba\n");

    render(<CollectionsToolView csrf="csrf-token" onBack={onBack} />);

    const backButton = screen.getByRole("button", { name: "Back" });

    fireEvent.click(backButton);

    expect(await screen.findByText("Favorites")).toBeTruthy();
    expect(onBack).toHaveBeenCalledTimes(1);
  });
});
