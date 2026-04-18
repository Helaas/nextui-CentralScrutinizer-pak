import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { ToolsView } from "./tools-view";

afterEach(() => {
  cleanup();
});

describe("ToolsView", () => {
  it("shows the terminal card as disabled when handheld access is off", () => {
    render(
      <ToolsView
        onOpenCollections={vi.fn()}
        onOpenFileBrowser={vi.fn()}
        onOpenLogs={vi.fn()}
        onOpenScreenshots={vi.fn()}
        onOpenTerminal={vi.fn()}
        terminalEnabled={false}
      />,
    );

    expect(screen.getByRole("button", { name: /Terminal/ })).toHaveProperty("disabled", true);
    expect(screen.getByText(/Enable on handheld/i)).toBeTruthy();
    expect(screen.getByText(/disabled on the handheld/i)).toBeTruthy();
  });

  it("opens tools when cards are clicked", () => {
    const onOpenCollections = vi.fn();
    const onOpenFileBrowser = vi.fn();
    const onOpenLogs = vi.fn();
    const onOpenScreenshots = vi.fn();
    const onOpenTerminal = vi.fn();

    render(
      <ToolsView
        onOpenCollections={onOpenCollections}
        onOpenFileBrowser={onOpenFileBrowser}
        onOpenLogs={onOpenLogs}
        onOpenScreenshots={onOpenScreenshots}
        onOpenTerminal={onOpenTerminal}
        terminalEnabled
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: /File Browser/ }));
    fireEvent.click(screen.getByRole("button", { name: /Collections/ }));
    fireEvent.click(screen.getByRole("button", { name: /Screenshots/ }));
    fireEvent.click(screen.getByRole("button", { name: /Log Viewer/ }));
    fireEvent.click(screen.getByRole("button", { name: /Terminal/ }));

    expect(onOpenFileBrowser).toHaveBeenCalledTimes(1);
    expect(onOpenCollections).toHaveBeenCalledTimes(1);
    expect(onOpenScreenshots).toHaveBeenCalledTimes(1);
    expect(onOpenLogs).toHaveBeenCalledTimes(1);
    expect(onOpenTerminal).toHaveBeenCalledTimes(1);
  });
});
