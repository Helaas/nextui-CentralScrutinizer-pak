import type { BrowserScope, ToolKey } from "./types";

export type AppDestination = "library" | "tools";

export type AppViewState =
  | { view: "pair" }
  | { view: "dashboard"; destination: "library" }
  | { view: "platform"; destination: "library"; tag: string }
  | { view: "states"; destination: "library"; tag: string }
  | {
      view: "browser";
      destination: "library";
      scope: Exclude<BrowserScope, "files">;
      tag: string;
      path?: string;
    }
  | { view: "tools"; destination: "tools"; tool?: ToolKey; path?: string };

export function getDestination(state: AppViewState): AppDestination | null {
  if (state.view === "pair") {
    return null;
  }

  return state.destination;
}

export function readShowEmptyPlatforms(search: string): boolean {
  const params = new URLSearchParams(search);

  return params.get("showEmpty") === "1";
}

export function readViewState(search: string): AppViewState {
  const params = new URLSearchParams(search);
  const view = params.get("view");
  const tag = params.get("tag") ?? "";
  const scope = params.get("scope") as BrowserScope | null;
  const path = params.get("path") ?? undefined;

  if (view === "platform" && tag) {
    return { view: "platform", destination: "library", tag };
  }
  if (view === "states" && tag) {
    return { view: "states", destination: "library", tag };
  }
  if (view === "browser" && tag && scope && scope !== "files") {
    return { view: "browser", destination: "library", scope, tag, path };
  }
  if (view === "files") {
    return { view: "tools", destination: "tools", tool: "file-browser", path };
  }
  if (view === "tools") {
    const tool = params.get("tool") as ToolKey | null;

    if (tool === "file-browser" || tool === "logs") {
      return { view: "tools", destination: "tools", tool, path };
    }
    if (tool === "terminal" || tool === "collections" || tool === "screenshots" || tool === "mac-dot-clean") {
      return { view: "tools", destination: "tools", tool };
    }

    return { view: "tools", destination: "tools" };
  }
  if (view === "pair") {
    return { view: "pair" };
  }

  return { view: "dashboard", destination: "library" };
}

export function writeViewState(
  state: AppViewState,
  options?: { showEmptyPlatforms?: boolean },
): string {
  const params = new URLSearchParams();

  params.set("view", state.view);
  if (state.view === "platform") {
    params.set("tag", state.tag);
  }
  if (state.view === "states") {
    params.set("tag", state.tag);
  }
  if (state.view === "browser") {
    params.set("scope", state.scope);
    params.set("tag", state.tag);
    if (state.path) {
      params.set("path", state.path);
    }
  }
  if (state.view === "tools" && state.tool) {
    params.set("tool", state.tool);
    if ((state.tool === "file-browser" || state.tool === "logs") && state.path) {
      params.set("path", state.path);
    }
  }
  if (state.view !== "pair" && state.destination === "library" && options?.showEmptyPlatforms) {
    params.set("showEmpty", "1");
  }

  return `?${params.toString()}`;
}
