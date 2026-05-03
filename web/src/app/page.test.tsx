import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => ({
  ApiError: class MockApiError extends Error {
    code?: string;
    status: number;

    constructor(message: string, status: number, code?: string) {
      super(message);
      this.name = "ApiError";
      this.status = status;
      this.code = code;
    }
  },
  PAIRING_UNAVAILABLE_MESSAGE:
    "Pairing is unavailable while the app is running in background mode. Reopen it on the handheld to pair or change settings.",
  UploadAbortedError: class MockUploadAbortedError extends Error {
    constructor() {
      super("Upload cancelled");
      this.name = "UploadAbortedError";
    }
  },
  beginUploadFilesBatched: vi.fn(),
  buildDownloadUrl: vi.fn(() => "/api/download?scope=roms&path=Pokemon%20Emerald.gba"),
  createFolder: vi.fn(),
  deleteItem: vi.fn(),
  getBrowser: vi.fn(),
  getBrowserAll: vi.fn(),
  getMacDotfiles: vi.fn(),
  getPlatforms: vi.fn(),
  getSaveStates: vi.fn(),
  getSession: vi.fn(),
  pairBrowser: vi.fn(),
  pairBrowserQr: vi.fn(),
  readTextFile: vi.fn(),
  replaceArt: vi.fn(),
  renameItem: vi.fn(),
  revokeBrowser: vi.fn(),
  writeTextFile: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);
vi.mock("../components/logs-tool-view", () => ({
  LogsToolView: ({ onBack }: { onBack: () => void }) => (
    <div>
      <p>Mock logs tool</p>
      <button onClick={onBack} type="button">
        Back
      </button>
    </div>
  ),
}));
vi.mock("../components/terminal-tool-view", () => ({
  TerminalToolView: ({ enabled, onBack }: { enabled: boolean; onBack: () => void }) => (
    <div>
      <p>{enabled ? "Mock terminal enabled" : "Mock terminal disabled"}</p>
      <button onClick={onBack} type="button">
        Back
      </button>
    </div>
  ),
}));
vi.mock("../components/save-states-view", () => ({
  SaveStatesView: ({ onBack, platform }: { onBack: () => void; platform: { name: string } }) => (
    <div>
      <p>Mock save states for {platform.name}</p>
      <button onClick={onBack} type="button">
        Back
      </button>
    </div>
  ),
}));
vi.mock("../components/mac-dot-clean-tool-view", () => ({
  MacDotCleanToolView: ({ onBack }: { onBack: () => void }) => (
    <div>
      <p>Mock Mac Dot Cleanup</p>
      <button onClick={onBack} type="button">
        Back
      </button>
    </div>
  ),
}));

import Page from "./page";

function createFileList(files: File[]): FileList {
  return {
    ...files,
    item: (index: number) => files[index] ?? null,
    length: files.length,
    [Symbol.iterator]: function* () {
      yield* files;
    },
  } as unknown as FileList;
}

function pairedSession(terminal = true) {
  return { paired: true, csrf: "csrf-token", trustedCount: 1, pairingAvailable: true, capabilities: { terminal } };
}

function supportedResources(overrides: Partial<Record<"roms" | "saves" | "states" | "bios" | "overlays" | "cheats", boolean>> = {}) {
  return {
    roms: true,
    saves: true,
    states: true,
    bios: true,
    overlays: true,
    cheats: true,
    ...overrides,
  };
}

function emulatorState(
  overrides: Partial<{ requiresEmulator: boolean; emulatorInstalled: boolean; emulatorWarning: string | null }> = {},
) {
  return {
    requiresEmulator: true,
    emulatorInstalled: true,
    emulatorWarning: null,
    ...overrides,
  };
}

function platformGroups() {
  return {
    groups: [
      {
        name: "Nintendo",
        platforms: [
          {
            tag: "GBA",
            name: "Game Boy Advance",
            group: "Nintendo",
            icon: "GBA",
            isCustom: false,
            ...emulatorState(),
            romPath: "Roms/Game Boy Advance (GBA)",
            savePath: "Saves/GBA",
            biosPath: "Bios/GBA",
            supportedResources: supportedResources(),
            counts: { roms: 2, saves: 1, states: 3, bios: 0, overlays: 0, cheats: 0 },
          },
        ],
      },
    ],
  };
}

function duplicatePlatformGroups() {
  return {
    groups: [
      {
        name: "Nintendo",
        platforms: [
          {
            tag: "GBA",
            name: "Game Boy Advance",
            group: "Nintendo",
            icon: "GBA",
            isCustom: false,
            ...emulatorState(),
            romPath: "Roms/Game Boy Advance (GBA)",
            savePath: "Saves/GBA",
            biosPath: "Bios/GBA",
            supportedResources: supportedResources(),
            counts: { roms: 2, saves: 1, states: 0, bios: 0, overlays: 0, cheats: 0 },
          },
          {
            tag: "MGBA",
            name: "Game Boy Advance",
            group: "Nintendo",
            icon: "MGBA",
            isCustom: false,
            ...emulatorState(),
            romPath: "Roms/Game Boy Advance (MGBA)",
            savePath: "Saves/MGBA",
            biosPath: "Bios/MGBA",
            supportedResources: supportedResources(),
            counts: { roms: 1, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
          },
        ],
      },
    ],
  };
}

function portsPlatformGroups() {
  return {
    groups: [
      {
        name: "PortMaster",
        platforms: [
          {
            tag: "PORTS",
            name: "Ports",
            group: "PortMaster",
            icon: "PORTMASTER",
            isCustom: false,
            ...emulatorState({ requiresEmulator: false }),
            romPath: "Roms/Ports (PORTS)",
            savePath: "Saves/PORTS",
            biosPath: "Bios/PORTS",
            supportedResources: supportedResources({
              saves: false,
              states: false,
              bios: false,
              overlays: false,
              cheats: false,
            }),
            counts: { roms: 2, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
          },
        ],
      },
    ],
  };
}

function platformGroupsWithPorts() {
  return {
    groups: [...platformGroups().groups, ...portsPlatformGroups().groups],
  };
}

function multipleMissingPlatformGroups() {
  return {
    groups: [
      {
        name: "Nintendo",
        platforms: [
          {
            tag: "GBA",
            name: "Game Boy Advance",
            group: "Nintendo",
            icon: "GBA",
            isCustom: false,
            ...emulatorState({ emulatorInstalled: false, emulatorWarning: "Missing GBA emulator." }),
            romPath: "Roms/Game Boy Advance (GBA)",
            savePath: "Saves/GBA",
            biosPath: "Bios/GBA",
            supportedResources: supportedResources(),
            counts: { roms: 2, saves: 1, states: 0, bios: 0, overlays: 0, cheats: 0 },
          },
          {
            tag: "MGBA",
            name: "Game Boy Advance",
            group: "Nintendo",
            icon: "MGBA",
            isCustom: false,
            ...emulatorState({ emulatorInstalled: false, emulatorWarning: "Missing MGBA emulator." }),
            romPath: "Roms/Game Boy Advance (MGBA)",
            savePath: "Saves/MGBA",
            biosPath: "Bios/MGBA",
            supportedResources: supportedResources(),
            counts: { roms: 1, saves: 0, states: 0, bios: 0, overlays: 0, cheats: 0 },
          },
        ],
      },
    ],
  };
}

function romBrowserResponse() {
  return {
    scope: "roms" as const,
    title: "ROMs - Game Boy Advance",
    rootPath: "Roms/Game Boy Advance (GBA)",
    path: "",
    breadcrumbs: [],
    totalCount: 0,
    offset: 0,
    truncated: false,
    entries: [
      {
        name: "Pokemon Emerald.gba",
        path: "Pokemon Emerald.gba",
        type: "rom",
        size: 1024,
        modified: 1_700_000_000,
        status: "",
        thumbnailPath: ".media/Pokemon Emerald.png",
      },
      {
        name: "Metroid Fusion.gba",
        path: "Metroid Fusion.gba",
        type: "rom",
        size: 2048,
        modified: 1_700_000_100,
        status: "",
        thumbnailPath: ".media/Metroid Fusion.png",
      },
    ],
  };
}

function savesBrowserResponse() {
  return {
    scope: "saves" as const,
    title: "Saves - Game Boy Advance",
    rootPath: "Saves/GBA",
    path: "",
    breadcrumbs: [],
    totalCount: 0,
    offset: 0,
    truncated: false,
    entries: [
      {
        name: "Pokemon Emerald.sav",
        path: "Pokemon Emerald.sav",
        type: "save",
        size: 1024,
        modified: 1_700_000_000,
        status: "",
        thumbnailPath: "",
      },
    ],
  };
}

function fileBrowserResponse(entries: Array<{
  name: string;
  path: string;
  type: string;
  size: number;
  modified: number;
  status: string;
  thumbnailPath: string;
}> = [], overrides: Partial<{ path: string; breadcrumbs: Array<{ label: string; path: string }>; truncated: boolean }> = {}) {
  return {
    scope: "files" as const,
    title: "Files",
    rootPath: "SD Card",
    path: "",
    breadcrumbs: [],
    totalCount: 0,
    offset: 0,
    truncated: false,
    entries,
    ...overrides,
  };
}

async function openTools() {
  const primaryNav = await screen.findByRole("navigation", { name: "Primary" });

  fireEvent.click(within(primaryNav).getByRole("button", { name: "Tools" }));
}

async function openFileBrowserTool() {
  await openTools();
  fireEvent.click(await screen.findByRole("button", { name: /File Browser/ }));
}

describe("Page", () => {
  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
    vi.clearAllMocks();
    vi.useRealTimers();
    window.history.replaceState(null, "", "/");
  });

  it("renders the pair screen when no trusted session exists", async () => {
    mockApi.getSession.mockResolvedValue({
      paired: false,
      csrf: null,
      trustedCount: 0,
      pairingAvailable: true,
      capabilities: { terminal: false },
    });

    render(<Page />);

    expect(await screen.findByLabelText("Pairing code")).toBeTruthy();
  });

  it("shows an informational pairing screen when background mode disables pairing", async () => {
    mockApi.getSession.mockResolvedValue({
      paired: false,
      csrf: null,
      trustedCount: 1,
      pairingAvailable: false,
      capabilities: { terminal: false },
    });

    render(<Page />);

    expect(await screen.findByText(/Background mode is active on the handheld/i)).toBeTruthy();
    expect(screen.queryByLabelText("Pairing code")).toBeNull();
    expect(screen.getByText(mockApi.PAIRING_UNAVAILABLE_MESSAGE)).toBeTruthy();
  });

  it("keeps retrying after an initial session outage and restores the active route", async () => {
    vi.useFakeTimers();
    window.history.replaceState(null, "", "/?view=browser&scope=roms&tag=GBA");
    mockApi.getSession.mockRejectedValueOnce(new Error("offline")).mockResolvedValueOnce(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(romBrowserResponse());

    render(<Page />);

    await act(async () => {});
    expect(screen.getByLabelText("Pairing code")).toBeTruthy();
    expect(screen.getByText(/Connection to the device is unavailable/i)).toBeTruthy();
    expect(window.location.search).toBe("?view=browser&scope=roms&tag=GBA");

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    await act(async () => {});
    expect(screen.getByRole("button", { name: "More actions for Pokemon Emerald.gba" })).toBeTruthy();
    expect(window.location.search).toBe("?view=browser&scope=roms&tag=GBA");
  }, 10000);

  it("consumes a QR pairing token on load and lands in the dashboard", async () => {
    window.history.replaceState(null, "", "/?pairQr=qr-token");
    mockApi.pairBrowserQr.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance")).toBeTruthy();
    expect(mockApi.pairBrowserQr).toHaveBeenCalledTimes(1);
    expect(mockApi.pairBrowserQr).toHaveBeenCalledWith("qr-token", expect.stringMatching(/^browser-/));
    expect(window.location.search).toBe("?view=dashboard");
  });

  it("walks from dashboard to platform to browser", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue({
      ...romBrowserResponse(),
      entries: [romBrowserResponse().entries[0]],
    });

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /Game Boy Advance/i }));
    expect(await screen.findByRole("button", { name: /ROMs/i })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /ROMs/i }));
    expect(await screen.findByRole("button", { name: "More actions for Pokemon Emerald.gba" })).toBeTruthy();
    expect(screen.getByRole("link", { name: "Download Pokemon Emerald.gba" })).toBeTruthy();
    expect(screen.getByPlaceholderText("Search in current folder")).toBeTruthy();
    expect(screen.queryByRole("checkbox")).toBeNull();
  });

  it("warns when a visible console is missing its emulator", async () => {
    window.history.replaceState(null, "", "/?view=dashboard&emu=all");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue({
      groups: [
        {
          name: "Nintendo",
          platforms: [
            {
              ...platformGroups().groups[0].platforms[0],
              ...emulatorState({ emulatorInstalled: false, emulatorWarning: "No emulator is installed for Game Boy Advance (GBA)." }),
            },
          ],
        },
      ],
    });

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance")).toBeTruthy();
    expect(screen.getByText("Missing emulator")).toBeTruthy();
    expect(screen.getByRole("combobox", { name: "Console filter" })).toHaveProperty("value", "all");
  });

  it("shows a missing-emulator banner in all mode and clears it after switching filters", async () => {
    window.history.replaceState(null, "", "/?view=dashboard&emu=all");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(multipleMissingPlatformGroups());

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance (GBA)")).toBeTruthy();
    expect(
      screen.getByText(
        /2 consoles have no installed emulator: Game Boy Advance \(GBA\), Game Boy Advance \(MGBA\)\./i,
      ),
    ).toBeTruthy();

    fireEvent.click(screen.getByRole("button", { name: "Show installed only" }));

    expect(window.location.search).toBe("?view=dashboard");
    expect(screen.queryByText(/2 consoles have no installed emulator/i)).toBeNull();
  });

  it("filters the dashboard to installed emulators and keeps Ports visible", async () => {
    window.history.replaceState(null, "", "/?view=dashboard&emu=all");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue({
      groups: [
        {
          name: "Nintendo",
          platforms: [
            {
              ...platformGroups().groups[0].platforms[0],
              ...emulatorState({ emulatorInstalled: false, emulatorWarning: "Missing GBA emulator." }),
            },
          ],
        },
        ...portsPlatformGroups().groups,
      ],
    });

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance")).toBeTruthy();
    fireEvent.change(screen.getByRole("combobox", { name: "Console filter" }), {
      target: { value: "installed" },
    });

    expect(window.location.search).toBe("?view=dashboard");
    expect(screen.queryByRole("button", { name: /Game Boy Advance/i })).toBeNull();
    expect(screen.getByRole("button", { name: /Ports/i })).toBeTruthy();
  });

  it("navigates into the dedicated save-states view", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    expect(await screen.findByText("Game Boy Advance")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /Game Boy Advance/i }));
    fireEvent.click(await screen.findByRole("button", { name: /Save States/i }));

    expect(await screen.findByText("Mock save states for Game Boy Advance")).toBeTruthy();
    expect(window.location.search).toBe("?view=states&tag=GBA");
  });

  it("shows only ROMs for Ports and rewrites unsupported routes back to the platform view", async () => {
    window.history.replaceState(null, "", "/?view=states&tag=PORTS");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(portsPlatformGroups());

    render(<Page />);

    expect(await screen.findByRole("heading", { level: 1, name: "Ports" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "ROMs" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Save States" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Saves" })).toBeNull();
    expect(screen.queryByRole("button", { name: "BIOS" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Overlays" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Cheats" })).toBeNull();
    await waitFor(() => {
      expect(window.location.search).toBe("?view=platform&tag=PORTS");
    });
  });

  it("keeps duplicate platform variants distinct in the library and header", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(duplicatePlatformGroups());

    render(<Page />);

    expect(await screen.findByRole("button", { name: /Game Boy Advance \(GBA\)/ })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /Game Boy Advance \(MGBA\)/ }));

    expect(await screen.findByRole("heading", { level: 1, name: "Game Boy Advance (MGBA)" })).toBeTruthy();
  });

  it("shows only library and tools in the shell and disables terminal from capabilities", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession(false));
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    const primaryNav = await screen.findByRole("navigation", { name: "Primary" });
    expect(within(primaryNav).getByRole("button", { name: "Library" })).toBeTruthy();
    expect(within(primaryNav).getByRole("button", { name: "Tools" })).toBeTruthy();
    expect(within(primaryNav).queryByRole("button", { name: "Files" })).toBeNull();

    fireEvent.click(within(primaryNav).getByRole("button", { name: "Tools" }));
    expect(await screen.findByRole("button", { name: /File Browser/ })).toBeTruthy();
    expect(screen.getByRole("button", { name: /Mac Dot Cleanup/ })).toBeTruthy();
    expect(screen.getByRole("button", { name: /Log Viewer/ })).toBeTruthy();
    expect(screen.getByRole("button", { name: /Terminal/ })).toHaveProperty("disabled", true);
    expect(screen.getByText(/Enable on handheld/i)).toBeTruthy();
  });

  it("opens the Mac Dot Cleanup tool from the tools workspace", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    await openTools();
    fireEvent.click(await screen.findByRole("button", { name: /Mac Dot Cleanup/ }));

    expect(await screen.findByText("Mock Mac Dot Cleanup")).toBeTruthy();
    expect(window.location.search).toBe("?view=tools&tool=mac-dot-clean");
  });

  it("returns to tools after opening the collections tool", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue({
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

    render(<Page />);

    await openTools();
    fireEvent.click(await screen.findByRole("button", { name: /Collections/ }));

    expect(await screen.findByRole("button", { name: "Back" })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools&tool=collections");

    fireEvent.click(screen.getByRole("button", { name: "Back" }));

    expect(await screen.findByRole("heading", { level: 1, name: "Tools" })).toBeTruthy();
    expect(screen.getByRole("button", { name: /Collections/ })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools");
  });

  it("returns to tools after opening the screenshots tool", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue({
      scope: "files",
      title: "Screenshots",
      rootPath: "Screenshots",
      path: "Screenshots",
      breadcrumbs: [],
      totalCount: 0,
      offset: 0,
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

    render(<Page />);

    await openTools();
    fireEvent.click(await screen.findByRole("button", { name: /Screenshots/ }));

    expect(await screen.findByRole("button", { name: "Back" })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools&tool=screenshots");

    fireEvent.click(screen.getByRole("button", { name: "Back" }));

    expect(await screen.findByRole("heading", { level: 1, name: "Tools" })).toBeTruthy();
    expect(screen.getByRole("button", { name: /Screenshots/ })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools");
  });

  it("syncs the tools workspace from the url and popstate history", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    window.history.replaceState(null, "", "/?view=tools");

    render(<Page />);

    expect(await screen.findByRole("button", { name: /File Browser/ })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools");

    window.history.replaceState(null, "", "/?view=dashboard");
    fireEvent(window, new PopStateEvent("popstate"));

    expect(await screen.findByPlaceholderText("Search platforms...")).toBeTruthy();
    expect(window.location.search).toBe("?view=dashboard");

    window.history.replaceState(null, "", "/?view=tools");
    fireEvent(window, new PopStateEvent("popstate"));

    expect(await screen.findByRole("button", { name: /File Browser/ })).toBeTruthy();
    expect(window.location.search).toBe("?view=tools");
  });

  it("supports the legacy files alias as tools file browser", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(
      fileBrowserResponse([
        {
          name: "saves",
          path: "saves",
          type: "directory",
          size: 0,
          modified: 1_700_000_000,
          status: "",
          thumbnailPath: "",
        },
      ]),
    );
    window.history.replaceState(null, "", "/?view=files&path=Saves");

    render(<Page />);

    expect(screen.queryByRole("textbox", { name: "Search" })).toBeNull();
    expect(screen.queryByRole("heading", { name: "Files" })).toBeNull();
    expect(await screen.findByRole("navigation", { name: "Files path" })).toBeTruthy();
    expect(await screen.findByPlaceholderText("Search in current folder")).toBeTruthy();
    expect(screen.getByText("1 item")).toBeTruthy();
    expect(screen.getByText("SD Card", { selector: "p.break-all" })).toBeTruthy();
    expect(window.location.search).toBe("?view=files&path=Saves");
  });

  it("returns to tools after opening the file browser tool", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(fileBrowserResponse());

    render(<Page />);

    await openFileBrowserTool();

    expect(await screen.findByRole("button", { name: "Back" })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Back" }));

    expect(await screen.findByRole("heading", { level: 1, name: "Tools" })).toBeTruthy();
    expect(screen.getByRole("button", { name: /File Browser/ })).toBeTruthy();
  });

  it("moves browser search into the browser workspace instead of the shell top bar", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue({
      ...romBrowserResponse(),
      entries: [romBrowserResponse().entries[0]],
    });

    render(<Page />);

    fireEvent.click(await screen.findByRole("button", { name: /Game Boy Advance/i }));
    fireEvent.click(await screen.findByRole("button", { name: /ROMs/i }));

    expect(screen.queryByRole("textbox", { name: "Search" })).toBeNull();
    expect(await screen.findByPlaceholderText("Search in current folder")).toBeTruthy();
  });

  it("deletes a files entry through the inline delete action from the tool workspace", async () => {
    vi.spyOn(window, "confirm").mockReturnValue(true);
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser
      .mockResolvedValueOnce(
        fileBrowserResponse([
          {
            name: "Saves",
            path: "Saves",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]),
      )
      .mockResolvedValueOnce(fileBrowserResponse());
    mockApi.deleteItem.mockResolvedValue(undefined);

    render(<Page />);

    await openFileBrowserTool();
    fireEvent.click(await screen.findByRole("button", { name: "Delete Saves" }));

    await screen.findByText("Deleted 1 item.");
    expect(mockApi.deleteItem).toHaveBeenCalledTimes(1);
    expect(mockApi.deleteItem).toHaveBeenCalledWith(
      expect.objectContaining({ scope: "files", tag: undefined, path: "Saves" }),
      "csrf-token",
    );
  });

  it("renames a files entry through the inline rename action from the tool workspace", async () => {
    vi.spyOn(window, "prompt").mockReturnValue("Archives");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser
      .mockResolvedValueOnce(
        fileBrowserResponse([
          {
            name: "Saves",
            path: "Saves",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]),
      )
      .mockResolvedValueOnce(
        fileBrowserResponse([
          {
            name: "Archives",
            path: "Archives",
            type: "directory",
            size: 0,
            modified: 1_700_000_100,
            status: "",
            thumbnailPath: "",
          },
        ]),
      );
    mockApi.renameItem.mockResolvedValue(undefined);

    render(<Page />);

    await openFileBrowserTool();
    fireEvent.click(await screen.findByRole("button", { name: "Rename Saves" }));

    await screen.findByText("Renamed Saves to Archives.");
    expect(mockApi.renameItem).toHaveBeenCalledTimes(1);
    expect(mockApi.renameItem).toHaveBeenCalledWith(
      expect.objectContaining({ scope: "files", tag: undefined, from: "Saves", to: "Archives" }),
      "csrf-token",
    );
  });

  it("warns before renaming a files entry to a name already visible in the folder", async () => {
    vi.spyOn(window, "prompt").mockReturnValue("Archives");
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValueOnce(
      fileBrowserResponse([
        {
          name: "Saves",
          path: "Saves",
          type: "directory",
          size: 0,
          modified: 1_700_000_000,
          status: "",
          thumbnailPath: "",
        },
        {
          name: "Archives",
          path: "Archives",
          type: "directory",
          size: 0,
          modified: 1_700_000_100,
          status: "",
          thumbnailPath: "",
        },
      ]),
    );

    render(<Page />);

    await openFileBrowserTool();
    fireEvent.click(await screen.findByRole("button", { name: "Rename Saves" }));

    await screen.findByText("Can't rename Saves to Archives because that name is already in use.");
    expect(mockApi.renameItem).not.toHaveBeenCalled();
  });

  it("moves selected files from the tool workspace through the bulk move action", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser
      .mockResolvedValueOnce(
        fileBrowserResponse([
          {
            name: "Pokemon Emerald.gba",
            path: "Pokemon Emerald.gba",
            type: "file",
            size: 1024,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
          {
            name: "Archives",
            path: "Archives",
            type: "directory",
            size: 0,
            modified: 1_700_000_100,
            status: "",
            thumbnailPath: "",
          },
        ]),
      )
      .mockResolvedValueOnce(fileBrowserResponse());
    mockApi.getBrowserAll.mockResolvedValueOnce(
      fileBrowserResponse(
        [],
        {
          path: "Archives",
          breadcrumbs: [{ label: "Archives", path: "Archives" }],
        },
      ),
    );
    mockApi.renameItem.mockResolvedValue(undefined);

    render(<Page />);

    await openFileBrowserTool();
    fireEvent.click(await screen.findByRole("checkbox", { name: "Select Pokemon Emerald.gba" }));
    fireEvent.click(screen.getByRole("button", { name: "Move Selected" }));
    fireEvent.click(await screen.findByRole("button", { name: "Open folder Archives" }));
    fireEvent.click(await screen.findByRole("button", { name: "Move Here" }));

    await screen.findByText("Moved 1 item to Archives.");
    expect(mockApi.renameItem).toHaveBeenCalledTimes(1);
    expect(mockApi.renameItem).toHaveBeenCalledWith(
      expect.objectContaining({
        scope: "files",
        tag: undefined,
        from: "Pokemon Emerald.gba",
        to: "Archives/Pokemon Emerald.gba",
      }),
      "csrf-token",
    );
  });

  it("marks the file browser tool busy during a manual refresh", async () => {
    let resolveRefresh: ((value: Awaited<ReturnType<typeof mockApi.getBrowser>>) => void) | undefined;

    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser
      .mockResolvedValueOnce(
        fileBrowserResponse([
          {
            name: "Saves",
            path: "Saves",
            type: "directory",
            size: 0,
            modified: 1_700_000_000,
            status: "",
            thumbnailPath: "",
          },
        ]),
      )
      .mockImplementationOnce(
        () =>
          new Promise((resolve) => {
            resolveRefresh = resolve;
          }),
      );

    render(<Page />);

    await openFileBrowserTool();
    await screen.findByRole("button", { name: "Delete Saves" });
    fireEvent.click(screen.getByRole("button", { name: "Refresh" }));

    await waitFor(() => {
      expect(screen.getByRole("button", { name: "Refresh" })).toHaveProperty("disabled", true);
      expect(screen.getByRole("button", { name: "Delete Saves" })).toHaveProperty("disabled", true);
      expect(screen.getByRole("button", { name: "Open Saves" })).toHaveProperty("disabled", true);
    });

    resolveRefresh?.(
      fileBrowserResponse([
        {
          name: "Saves",
          path: "Saves",
          type: "directory",
          size: 0,
          modified: 1_700_000_000,
          status: "",
          thumbnailPath: "",
        },
      ]),
    );

    await waitFor(() => {
      expect(screen.getByRole("button", { name: "Refresh" })).toHaveProperty("disabled", false);
    });
  });

  it("uploads a picked folder from the file browser tool when directory upload is supported", async () => {
    const folderFile = new File(["rom"], "Pokemon Emerald.gba", { type: "application/octet-stream" }) as File & {
      webkitRelativePath?: string;
    };
    const originalCreateElement = document.createElement.bind(document);

    Object.defineProperty(folderFile, "webkitRelativePath", {
      configurable: true,
      value: "Favorites/GBA/Pokemon Emerald.gba",
    });

    vi.spyOn(document, "createElement").mockImplementation(((tagName: string, options?: ElementCreationOptions) => {
      const element = originalCreateElement(tagName, options);

      if (tagName.toLowerCase() !== "input") {
        return element;
      }

      const input = element as HTMLInputElement & { webkitdirectory?: boolean };

      Object.defineProperty(input, "webkitdirectory", {
        configurable: true,
        enumerable: true,
        value: false,
        writable: true,
      });

      input.click = () => {
        Object.defineProperty(input, "files", {
          configurable: true,
          value: createFileList([folderFile]),
        });
        fireEvent.change(input);
      };

      return input;
    }) as typeof document.createElement);

    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(fileBrowserResponse());
    mockApi.beginUploadFilesBatched.mockReturnValue({
      cancel: vi.fn(),
      promise: Promise.resolve({ uploaded: 1, failed: 0, cancelled: false }),
    });

    render(<Page />);

    await openFileBrowserTool();
    fireEvent.click(await screen.findByRole("button", { name: "Upload Folder" }));

    await screen.findByText("Uploaded 1 file.");
    expect(mockApi.beginUploadFilesBatched).toHaveBeenCalledTimes(1);
    expect(mockApi.beginUploadFilesBatched).toHaveBeenCalledWith(
      expect.objectContaining({
        files: [folderFile],
        path: undefined,
        scope: "files",
      }),
      "csrf-token",
      expect.any(Function),
    );
    expect(folderFile.webkitRelativePath).toBe("Favorites/GBA/Pokemon Emerald.gba");
  });

  it("shows a cancel action for uploads and reports when the upload is cancelled", async () => {
    let resolveUpload: ((summary: { uploaded: number; failed: number; cancelled: boolean }) => void) | undefined;
    const cancel = vi.fn(() => {
      resolveUpload?.({ uploaded: 0, failed: 0, cancelled: true });
    });

    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(fileBrowserResponse());
    mockApi.beginUploadFilesBatched.mockImplementation(() => ({
      cancel,
      promise: new Promise<{ uploaded: number; failed: number; cancelled: boolean }>((resolve) => {
        resolveUpload = resolve;
      }),
    }));

    render(<Page />);

    await openFileBrowserTool();
    expect(await screen.findByRole("button", { name: "Upload File" })).toBeTruthy();
    const uploadInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    const uploadFile = new File(["save"], "state.sav", { type: "application/octet-stream" });

    fireEvent.change(uploadInput, { target: { files: createFileList([uploadFile]) } });
    expect(await screen.findByRole("button", { name: "Cancel Upload" })).toBeTruthy();

    fireEvent.click(screen.getByRole("button", { name: "Cancel Upload" }));

    await screen.findByText("Upload cancelled.");
    expect(cancel).toHaveBeenCalledTimes(1);
  });

  it("resumes the current browser route after reconnect", async () => {
    vi.useFakeTimers();
    let resolveReconnect: ((value: ReturnType<typeof pairedSession>) => void) | undefined;

    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getSession
      .mockResolvedValueOnce(pairedSession())
      .mockRejectedValueOnce(new Error("offline"))
      .mockImplementationOnce(
        () =>
          new Promise((resolve) => {
            resolveReconnect = resolve as (value: ReturnType<typeof pairedSession>) => void;
          }),
      );
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(romBrowserResponse());

    render(<Page />);

    await act(async () => {});
    fireEvent.click(screen.getByRole("button", { name: /Game Boy Advance/i }));
    await act(async () => {});
    fireEvent.click(screen.getByRole("button", { name: /ROMs/i }));
    await act(async () => {});
    expect(screen.getByRole("button", { name: "More actions for Pokemon Emerald.gba" })).toBeTruthy();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });

    resolveReconnect?.(pairedSession());
    await act(async () => {});
    expect(screen.queryByText("Connection lost")).toBeNull();
    expect(screen.getByRole("button", { name: "More actions for Pokemon Emerald.gba" })).toBeTruthy();
    expect(mockApi.getSession.mock.calls.length).toBeGreaterThanOrEqual(3);
  }, 10000);

  it("returns to pairing with a reconnect message when the browser is no longer trusted", async () => {
    vi.useFakeTimers();
    mockApi.getSession
      .mockResolvedValueOnce(pairedSession())
      .mockRejectedValueOnce(new Error("offline"))
      .mockResolvedValueOnce({
        paired: false,
        csrf: null,
        trustedCount: 0,
        pairingAvailable: true,
        capabilities: { terminal: false },
      });
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    await act(async () => {});
    expect(screen.getByText("Game Boy Advance")).toBeTruthy();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    expect(screen.getByText("Connection lost")).toBeTruthy();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2000);
    });
    await act(async () => {});
    expect(screen.getByLabelText("Pairing code")).toBeTruthy();
    expect(screen.getByText(/Connection restored, but this browser is no longer trusted/i)).toBeTruthy();
  }, 10000);

  it("switches to the handheld recovery message when a paired session loses pairing availability during polling", async () => {
    vi.useFakeTimers();
    mockApi.getSession
      .mockResolvedValueOnce(pairedSession())
      .mockResolvedValueOnce({
        paired: false,
        csrf: null,
        trustedCount: 1,
        pairingAvailable: false,
        capabilities: { terminal: false },
      });
    mockApi.getPlatforms.mockResolvedValue(platformGroups());

    render(<Page />);

    await act(async () => {});
    expect(screen.getByText("Game Boy Advance")).toBeTruthy();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(5000);
    });
    await act(async () => {});

    expect(screen.getByText(mockApi.PAIRING_UNAVAILABLE_MESSAGE)).toBeTruthy();
    expect(screen.queryByLabelText("Pairing code")).toBeNull();
  }, 10000);

  it("switches to the handheld recovery message when pairing becomes unavailable", async () => {
    mockApi.getSession.mockResolvedValue({
      paired: false,
      csrf: null,
      trustedCount: 1,
      pairingAvailable: true,
      capabilities: { terminal: false },
    });
    mockApi.pairBrowser.mockRejectedValue(
      new mockApi.ApiError(mockApi.PAIRING_UNAVAILABLE_MESSAGE, 403, "pairing_unavailable"),
    );

    render(<Page />);

    fireEvent.change(await screen.findByLabelText("Pairing code"), { target: { value: "7391" } });
    fireEvent.click(screen.getByRole("button", { name: "Pair Browser" }));

    expect(await screen.findByText(mockApi.PAIRING_UNAVAILABLE_MESSAGE)).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Pair Browser" })).toBeNull();
  });

  it("replaces art through the dedicated png helper", async () => {
    const artFile = new File(["png"], "Pokemon Emerald.png", { type: "image/png" });
    const originalCreateElement = document.createElement.bind(document);

    vi.spyOn(document, "createElement").mockImplementation(((tagName: string, options?: ElementCreationOptions) => {
      const element = originalCreateElement(tagName, options);

      if (tagName.toLowerCase() !== "input") {
        return element;
      }

      const input = element as HTMLInputElement;

      input.click = () => {
        Object.defineProperty(input, "files", {
          configurable: true,
          value: createFileList([artFile]),
        });
        fireEvent.change(input);
      };

      return input;
    }) as typeof document.createElement);

    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(romBrowserResponse());
    mockApi.replaceArt.mockResolvedValue(undefined);

    render(<Page />);

    fireEvent.click(await screen.findByRole("button", { name: /Game Boy Advance/i }));
    fireEvent.click(await screen.findByRole("button", { name: /ROMs/i }));
    fireEvent.click(await screen.findByRole("button", { name: "More actions for Pokemon Emerald.gba" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Replace Art" }));

    await screen.findByText("Artwork updated for Pokemon Emerald.gba.");
    expect(mockApi.replaceArt).toHaveBeenCalledTimes(1);
    expect(mockApi.replaceArt).toHaveBeenCalledWith(
      expect.objectContaining({
        tag: "GBA",
        path: "Pokemon Emerald.gba",
        file: artFile,
      }),
      "csrf-token",
      expect.any(Function),
    );
  });

  it("disconnects with the session csrf token", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.revokeBrowser.mockResolvedValue(undefined);

    render(<Page />);

    fireEvent.click(await screen.findByRole("button", { name: "Disconnect" }));

    await waitFor(() => {
      expect(mockApi.revokeBrowser).toHaveBeenCalledWith("csrf-token");
    });
    expect(await screen.findByLabelText("Pairing code")).toBeTruthy();
  });

  it("deletes a library row through the overflow menu", async () => {
    vi.spyOn(window, "confirm").mockReturnValue(true);
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser
      .mockResolvedValueOnce(romBrowserResponse())
      .mockResolvedValueOnce({
        ...romBrowserResponse(),
        entries: [romBrowserResponse().entries[1]],
      });
    mockApi.deleteItem.mockResolvedValue(undefined);

    render(<Page />);

    fireEvent.click(await screen.findByRole("button", { name: /Game Boy Advance/i }));
    fireEvent.click(await screen.findByRole("button", { name: /ROMs/i }));
    fireEvent.click(await screen.findByRole("button", { name: "More actions for Pokemon Emerald.gba" }));
    fireEvent.click(screen.getByRole("menuitem", { name: "Delete" }));

    await screen.findByText("Deleted 1 item.");
    expect(mockApi.deleteItem).toHaveBeenCalledTimes(1);
    expect(mockApi.deleteItem).toHaveBeenCalledWith(
      expect.objectContaining({ scope: "roms", tag: "GBA", path: "Pokemon Emerald.gba" }),
      "csrf-token",
    );
    expect(screen.queryByRole("link", { name: "Download Pokemon Emerald.gba" })).toBeNull();
    expect(screen.getByRole("link", { name: "Download Metroid Fusion.gba" })).toBeTruthy();
  });

  it("hides folder-create and folder-upload actions outside ROMs in the library browser", async () => {
    mockApi.getSession.mockResolvedValue(pairedSession());
    mockApi.getPlatforms.mockResolvedValue(platformGroups());
    mockApi.getBrowser.mockResolvedValue(savesBrowserResponse());

    render(<Page />);

    fireEvent.click(await screen.findByRole("button", { name: /Game Boy Advance/i }));
    fireEvent.click(await screen.findByRole("button", { name: /Saves/i }));

    expect(await screen.findByRole("button", { name: "Upload File" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Refresh" })).toBeTruthy();
    expect(screen.getByPlaceholderText("Search in current folder")).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Upload Folder" })).toBeNull();
    expect(screen.queryByRole("button", { name: "New Folder" })).toBeNull();
    expect(screen.queryByRole("checkbox")).toBeNull();
  });
});
