import { cleanup, render, screen } from "@testing-library/react";
import { within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { AppShell } from "./app-shell";

afterEach(() => {
  cleanup();
});

describe("AppShell", () => {
  it("renders library and tools nav plus search for library screens", () => {
    render(
      <AppShell
        actions={<button type="button">Sync</button>}
        description="Manage content by platform."
        destination="library"
        onDestinationChange={vi.fn()}
        onDisconnect={vi.fn()}
        onSearchChange={vi.fn()}
        searchPlaceholder="Search platforms..."
        searchValue="gba"
        showSearch
        title="Library"
        transfer={{ active: true, label: "Uploading 1 file", progress: 60 }}
      >
        <div>Library content</div>
      </AppShell>,
    );

    const primaryNav = screen.getByRole("navigation", { name: "Primary" });
    const mobileNav = screen.getByRole("navigation", { name: "Mobile" });

    expect(within(primaryNav).getByRole("button", { name: "Library" }).getAttribute("aria-current")).toBe("page");
    expect(within(primaryNav).getByRole("button", { name: "Tools" })).toBeTruthy();
    expect(within(mobileNav).getByRole("button", { name: "Library" }).getAttribute("aria-current")).toBe("page");
    expect(within(mobileNav).getByRole("button", { name: "Tools" })).toBeTruthy();
    expect(within(mobileNav).queryByRole("button", { name: "Files" })).toBeNull();
    expect((screen.getByRole("textbox", { name: "Search" }) as HTMLInputElement).value).toBe("gba");
    expect(screen.getByText(/Uploading 1 file/)).toBeTruthy();
    expect(screen.getByRole("button", { name: "Sync" })).toBeTruthy();
  });

  it("hides search for tools screens", () => {
    render(
      <AppShell
        description="Maintenance tools."
        destination="tools"
        onDestinationChange={vi.fn()}
        onDisconnect={vi.fn()}
        onSearchChange={vi.fn()}
        searchPlaceholder="Search"
        searchValue=""
        showSearch={false}
        title="Tools"
        transfer={{ active: false, label: "", progress: 0 }}
      >
        <div>Tools content</div>
      </AppShell>,
    );

    expect(screen.queryByRole("textbox", { name: "Search" })).toBeNull();
    const primaryNav = screen.getByRole("navigation", { name: "Primary" });
    const mobileNav = screen.getByRole("navigation", { name: "Mobile" });
    expect(within(primaryNav).getByRole("button", { name: "Tools" }).getAttribute("aria-current")).toBe("page");
    expect(within(mobileNav).getByRole("button", { name: "Tools" }).getAttribute("aria-current")).toBe("page");
    expect(within(mobileNav).getByRole("button", { name: "Library" })).toBeTruthy();
    expect(within(mobileNav).queryByRole("button", { name: "Files" })).toBeNull();
  });

  it("can hide the generic page header for tool-owned workspace content", () => {
    render(
      <AppShell
        description="Browse the device filesystem."
        destination="tools"
        onDestinationChange={vi.fn()}
        onDisconnect={vi.fn()}
        onSearchChange={vi.fn()}
        searchPlaceholder="Search"
        searchValue=""
        showPageHeader={false}
        showSearch={false}
        title="File Browser"
        transfer={{ active: false, label: "", progress: 0 }}
      >
        <div>Browser workspace body</div>
      </AppShell>,
    );

    expect(screen.queryByText("Workspace")).toBeNull();
    expect(screen.getByText("Browser workspace body")).toBeTruthy();
  });

  it("can constrain tool content to the viewport for full-height tools", () => {
    const { container } = render(
      <AppShell
        description="Open a shell."
        destination="tools"
        fillViewport
        onDestinationChange={vi.fn()}
        onDisconnect={vi.fn()}
        onSearchChange={vi.fn()}
        searchPlaceholder="Search"
        searchValue=""
        showPageHeader={false}
        showSearch={false}
        title="Terminal"
        transfer={{ active: false, label: "", progress: 0 }}
      >
        <div>Terminal body</div>
      </AppShell>,
    );

    const main = container.querySelector("main");
    const body = screen.getByText("Terminal body").parentElement;
    const primaryNav = screen.getByRole("navigation", { name: "Primary" });

    expect(main?.className).toContain("h-[100dvh]");
    expect(main?.className).toContain("overflow-hidden");
    expect(body?.className).toContain("flex-1");
    expect(body?.className).toContain("overflow-hidden");
    expect(screen.getByAltText("The Central Scrutinizer").className).toContain("h-14");
    expect(primaryNav.parentElement?.className).toContain("hidden");
    expect(primaryNav.parentElement?.className).toContain("md:flex");
  });
});
