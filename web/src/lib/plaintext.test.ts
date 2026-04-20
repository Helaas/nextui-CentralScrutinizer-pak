import { describe, expect, it } from "vitest";

import { isPlaintextFileName } from "./plaintext";

describe("isPlaintextFileName", () => {
  it("recognizes common plaintext extensions regardless of case", () => {
    expect(isPlaintextFileName("notes.txt")).toBe(true);
    expect(isPlaintextFileName("config.INI")).toBe(true);
    expect(isPlaintextFileName("script.Lua")).toBe(true);
    expect(isPlaintextFileName("payload.json")).toBe(true);
  });

  it("rejects binary or unknown extensions", () => {
    expect(isPlaintextFileName("rom.gba")).toBe(false);
    expect(isPlaintextFileName("archive.zip")).toBe(false);
    expect(isPlaintextFileName("README")).toBe(false);
    expect(isPlaintextFileName(".hidden")).toBe(false);
    expect(isPlaintextFileName("trailing.dot.")).toBe(false);
  });
});
