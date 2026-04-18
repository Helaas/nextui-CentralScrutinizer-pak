import { afterEach, describe, expect, it, vi } from "vitest";

import {
  beginUploadFiles,
  buildDownloadUrl,
  getBrowser,
  getPlatforms,
  getLogs,
  getStatus,
  pairBrowser,
  pairBrowserQr,
  readTextFile,
  replaceArt,
  revokeBrowser,
  searchFiles,
  uploadFiles,
  UploadAbortedError,
} from "./api";

class MockXhr {
  static instances: MockXhr[] = [];
  static autoLoad = true;

  headers: Record<string, string> = {};
  listeners: Record<string, () => void> = {};
  method = "";
  status = 200;
  uploadListener?: (event: { lengthComputable: boolean; loaded: number; total: number }) => void;
  url = "";
  body: FormData | null = null;
  upload = {
    addEventListener: (_event: string, listener: (event: { lengthComputable: boolean; loaded: number; total: number }) => void) => {
      this.uploadListener = listener;
    },
  };

  constructor() {
    MockXhr.instances.push(this);
  }

  open(method: string, url: string) {
    this.method = method;
    this.url = url;
  }

  setRequestHeader(key: string, value: string) {
    this.headers[key] = value;
  }

  addEventListener(event: string, listener: () => void) {
    this.listeners[event] = listener;
  }

  send(body: FormData) {
    this.body = body;
    this.uploadListener?.({ lengthComputable: true, loaded: 5, total: 10 });
    if (MockXhr.autoLoad) {
      this.listeners.load?.();
    }
  }

  abort() {
    this.listeners.abort?.();
  }
}

describe("buildDownloadUrl", () => {
  it("builds scoped download URLs", () => {
    expect(buildDownloadUrl("roms", "Pokemon Emerald.gba", "GBA")).toBe(
      "/api/download?scope=roms&tag=GBA&path=Pokemon+Emerald.gba",
    );
  });

  it("adds csrf to raw download URLs when requested", () => {
    expect(buildDownloadUrl("files", "Screenshots/shot.png", undefined, "csrf-token")).toBe(
      "/api/download?scope=files&path=Screenshots%2Fshot.png&csrf=csrf-token",
    );
  });
});

describe("pairBrowser", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("posts the pairing code as urlencoded form data", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ paired: true, csrf: "csrf-token", trustedCount: 1 }),
    });

    vi.stubGlobal("fetch", fetchMock);

    await pairBrowser("7391", "browser-1");

    expect(fetchMock).toHaveBeenCalledWith(
      "/api/pair",
      expect.objectContaining({
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "browser_id=browser-1&code=7391",
      }),
    );
  });
});

describe("pairBrowserQr", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("posts the QR token and browser id as urlencoded form data", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ paired: true, csrf: "csrf-token", trustedCount: 1 }),
    });

    vi.stubGlobal("fetch", fetchMock);

    await pairBrowserQr("qr-token", "browser-1");

    expect(fetchMock).toHaveBeenCalledWith(
      "/api/pair",
      expect.objectContaining({
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "browser_id=browser-1&qr_token=qr-token",
      }),
    );
  });
});

describe("revokeBrowser", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("posts revoke with the csrf header", async () => {
    const fetchMock = vi.fn().mockResolvedValue({ ok: true });

    vi.stubGlobal("fetch", fetchMock);

    await revokeBrowser("csrf-token");

    expect(fetchMock).toHaveBeenCalledWith(
      "/api/revoke",
      expect.objectContaining({
        method: "POST",
        headers: { "X-CS-CSRF": "csrf-token" },
      }),
    );
  });
});

describe("authenticated GET helpers", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("sends csrf headers for protected JSON fetches", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ platform: "mac", port: 8877, trustedCount: 0, groups: [], files: [], results: [] }),
    });

    vi.stubGlobal("fetch", fetchMock);

    await getStatus();
    await getPlatforms("csrf-token");
    await getBrowser("files", "csrf-token", undefined, "Screenshots");
    await getLogs("csrf-token");
    await searchFiles("Screenshots", "shot", "csrf-token");

    expect(fetchMock).toHaveBeenNthCalledWith(1, "/api/status");
    expect(fetchMock).toHaveBeenNthCalledWith(
      2,
      "/api/platforms",
      expect.objectContaining({ headers: { "X-CS-CSRF": "csrf-token" } }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      3,
      "/api/browser?scope=files&path=Screenshots",
      expect.objectContaining({ headers: { "X-CS-CSRF": "csrf-token" } }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      4,
      "/api/logs",
      expect.objectContaining({ headers: { "X-CS-CSRF": "csrf-token" } }),
    );
    expect(fetchMock).toHaveBeenNthCalledWith(
      5,
      "/api/files/search?path=Screenshots&q=shot",
      expect.objectContaining({ headers: { "X-CS-CSRF": "csrf-token" } }),
    );
  });

  it("uses csrf for text-file reads", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      text: async () => "payload",
    });

    vi.stubGlobal("fetch", fetchMock);

    await expect(readTextFile("files", "Collections/Favorites.txt", "csrf-token")).resolves.toBe("payload");
    expect(fetchMock).toHaveBeenCalledWith(
      "/api/download?scope=files&path=Collections%2FFavorites.txt&csrf=csrf-token",
      expect.objectContaining({ headers: { "X-CS-CSRF": "csrf-token" } }),
    );
  });
});

describe("uploadFiles", () => {
  afterEach(() => {
    MockXhr.instances = [];
    MockXhr.autoLoad = true;
    vi.unstubAllGlobals();
  });

  it("posts scoped form data with the csrf header", async () => {
    const file = new File(["rom"], "Pokemon Emerald.gba", { type: "application/octet-stream" });
    let progressValue = 0;

    vi.stubGlobal("XMLHttpRequest", MockXhr as unknown as typeof XMLHttpRequest);

    await uploadFiles(
      {
        scope: "roms",
        tag: "GBA",
        path: "",
        files: [file],
      },
      "csrf-token",
      (progress) => {
        progressValue = progress;
      },
    );

    const request = MockXhr.instances[0];

    expect(request.method).toBe("POST");
    expect(request.url).toBe("/api/upload");
    expect(request.headers["X-CS-CSRF"]).toBe("csrf-token");
    expect((request.body?.get("scope") as string) ?? "").toBe("roms");
    expect((request.body?.get("tag") as string) ?? "").toBe("GBA");
    expect(((request.body?.get("file") as File) ?? file).name).toBe("Pokemon Emerald.gba");
    expect(progressValue).toBe(50);
  });

  it("preserves webkitRelativePath in multipart filenames for directory uploads", async () => {
    const file = new File(["rom"], "Pokemon Emerald.gba", { type: "application/octet-stream" });

    Object.defineProperty(file, "webkitRelativePath", {
      configurable: true,
      value: "Favorites/GBA/Pokemon Emerald.gba",
    });

    vi.stubGlobal("XMLHttpRequest", MockXhr as unknown as typeof XMLHttpRequest);

    await uploadFiles(
      {
        scope: "files",
        path: "Imports",
        files: [file],
      },
      "csrf-token",
    );

    const request = MockXhr.instances[0];

    expect(((request.body?.get("file") as File) ?? file).name).toBe("Favorites/GBA/Pokemon Emerald.gba");
  });

  it("can abort an upload in progress", async () => {
    MockXhr.autoLoad = false;
    vi.stubGlobal("XMLHttpRequest", MockXhr as unknown as typeof XMLHttpRequest);

    const handle = beginUploadFiles(
      {
        scope: "files",
        files: [new File(["payload"], "test.txt", { type: "text/plain" })],
      },
      "csrf-token",
    );

    handle.cancel();

    await expect(handle.promise).rejects.toBeInstanceOf(UploadAbortedError);
  });
});

describe("replaceArt", () => {
  afterEach(() => {
    MockXhr.instances = [];
    vi.unstubAllGlobals();
  });

  it("posts png art replacement form data with the csrf header", async () => {
    const file = new File(["png"], "Pokemon Emerald.png", { type: "image/png" });
    let progressValue = 0;

    vi.stubGlobal("XMLHttpRequest", MockXhr as unknown as typeof XMLHttpRequest);

    await replaceArt(
      {
        tag: "GBA",
        path: "Pokemon Emerald.gba",
        file,
      },
      "csrf-token",
      (progress) => {
        progressValue = progress;
      },
    );

    const request = MockXhr.instances[0];

    expect(request.method).toBe("POST");
    expect(request.url).toBe("/api/art/replace");
    expect(request.headers["X-CS-CSRF"]).toBe("csrf-token");
    expect((request.body?.get("tag") as string) ?? "").toBe("GBA");
    expect((request.body?.get("path") as string) ?? "").toBe("Pokemon Emerald.gba");
    expect(((request.body?.get("file") as File) ?? file).name).toBe("Pokemon Emerald.png");
    expect(progressValue).toBe(50);
  });
});
