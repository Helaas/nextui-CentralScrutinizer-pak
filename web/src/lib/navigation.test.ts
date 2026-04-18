import { describe, expect, it } from "vitest";

import { getDestination, readShowEmptyPlatforms, readViewState, writeViewState } from "./navigation";

describe("navigation", () => {
  it("maps the legacy files route into the tools file browser", () => {
    expect(readViewState("?view=files&path=Roms")).toEqual({
      view: "tools",
      destination: "tools",
      tool: "file-browser",
      path: "Roms",
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
    expect(writeViewState({ view: "tools", destination: "tools" })).toBe("?view=tools");
    expect(writeViewState({ view: "tools", destination: "tools", tool: "file-browser", path: "Roms" })).toBe(
      "?view=tools&tool=file-browser&path=Roms",
    );
    expect(writeViewState({ view: "tools", destination: "tools", tool: "terminal" })).toBe(
      "?view=tools&tool=terminal",
    );
    expect(writeViewState({ view: "tools", destination: "tools", tool: "collections" })).toBe(
      "?view=tools&tool=collections",
    );
  });

  it("derives library destination from nested library views", () => {
    expect(getDestination({ view: "pair" })).toBe(null);
    expect(getDestination({ view: "platform", destination: "library", tag: "GBA" })).toBe("library");
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
});
