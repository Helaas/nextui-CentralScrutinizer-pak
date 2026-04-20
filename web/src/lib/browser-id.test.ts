import { describe, expect, it, vi } from "vitest";

import { getBrowserId } from "./browser-id";

describe("getBrowserId", () => {
  it("reuses an existing stored browser id", () => {
    const storage = {
      getItem: vi.fn(() => "browser-existing"),
      setItem: vi.fn(),
    };

    expect(getBrowserId(storage)).toBe("browser-existing");
    expect(storage.setItem).not.toHaveBeenCalled();
  });

  it("creates and stores a new browser id when missing", () => {
    const storage = {
      getItem: vi.fn(() => null),
      setItem: vi.fn(),
    };

    const browserId = getBrowserId(storage);

    expect(browserId).toMatch(/^browser-[A-Za-z0-9-]+$/);
    expect(storage.setItem).toHaveBeenCalledWith("cs_browser_id", browserId);
  });
});
