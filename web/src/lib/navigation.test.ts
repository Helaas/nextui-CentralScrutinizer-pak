import { describe, expect, it } from "vitest";

import { getDestination, readLibraryEmuFilter, readShowEmptyPlatforms, readViewState, writeViewState } from "./navigation";

describe("navigation", () => {
  it("maps the legacy files route into the tools file browser", () => {
    expect(readViewState("?view=files&path=Roms")).toEqual({
      view: "tools",
      destination: "tools",
      tool: "file-browser",
      path: "Roms",
    });
    expect(readViewState("?view=states&tag=GBA")).toEqual({
      view: "states",
      destination: "library",
      tag: "GBA",
    });
    expect(readViewState("?view=tools&tool=logs&path=.userdata/logs/app.log")).toEqual({
      view: "tools",
      destination: "tools",
      tool: "logs",
      path: ".userdata/logs/app.log",
    });
  });

  it("writes canonical tool urls", () => {
    expect(writeViewState({ view: "dashboard", destination: "library" })).toBe("?view=dashboard");
    expect(writeViewState({ view: "dashboard", destination: "library" }, { showEmptyPlatforms: true })).toBe(
      "?view=dashboard&showEmpty=1",
    );
    expect(writeViewState({ view: "dashboard", destination: "library" }, { libraryEmuFilter: "all" })).toBe(
      "?view=dashboard&emu=all",
    );
    expect(writeViewState({ view: "tools", destination: "tools" })).toBe("?view=tools");
    expect(writeViewState({ view: "states", destination: "library", tag: "GBA" })).toBe("?view=states&tag=GBA");
    expect(writeViewState({ view: "tools", destination: "tools", tool: "file-browser", path: "Roms" })).toBe(
      "?view=tools&tool=file-browser&path=Roms",
    );
    expect(writeViewState({ view: "tools", destination: "tools", tool: "terminal" })).toBe(
      "?view=tools&tool=terminal",
    );
    expect(writeViewState({ view: "tools", destination: "tools", tool: "collections" })).toBe(
      "?view=tools&tool=collections",
    );
    expect(writeViewState({ view: "tools", destination: "tools", tool: "mac-dot-clean" })).toBe(
      "?view=tools&tool=mac-dot-clean",
    );
  });

  it("derives library destination from nested library views", () => {
    expect(getDestination({ view: "pair" })).toBe(null);
    expect(getDestination({ view: "platform", destination: "library", tag: "GBA" })).toBe("library");
    expect(getDestination({ view: "states", destination: "library", tag: "GBA" })).toBe("library");
    expect(
      getDestination({
        view: "browser",
        destination: "library",
        scope: "roms",
        tag: "GBA",
        path: "Pokemon Emerald.gba",
      }),
    ).toBe("library");
    expect(getDestination({ view: "tools", destination: "tools", tool: "file-browser" })).toBe("tools");
  });

  it("reads the empty-platform toggle from the url", () => {
    expect(readShowEmptyPlatforms("?view=dashboard&showEmpty=1")).toBe(true);
    expect(readShowEmptyPlatforms("?view=dashboard")).toBe(false);
  });

  it("reads the emulator filter from the url", () => {
    expect(readLibraryEmuFilter("?view=dashboard&emu=installed")).toBe("installed");
    expect(readLibraryEmuFilter("?view=dashboard")).toBe("installed");
    expect(readLibraryEmuFilter("?view=dashboard&emu=all")).toBe("all");
  });
});
