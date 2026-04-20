const BROWSER_ID_KEY = "cs_browser_id";
const BROWSER_ID_PATTERN = /^[A-Za-z0-9._-]+$/;
const fallbackValues = new Map<string, string>();

type BrowserIdStorage = Pick<Storage, "getItem" | "setItem">;

function makeRandomHex(bytes: number): string {
  if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
    const data = new Uint8Array(bytes);

    crypto.getRandomValues(data);
    return Array.from(data, (value) => value.toString(16).padStart(2, "0")).join("");
  }

  return Array.from({ length: bytes }, () => Math.floor(Math.random() * 256).toString(16).padStart(2, "0")).join("");
}

function makeBrowserId(): string {
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function") {
    return `browser-${crypto.randomUUID()}`;
  }

  return `browser-${makeRandomHex(16)}`;
}

function isValidBrowserId(value: string | null): value is string {
  return typeof value === "string" && value.length > 0 && value.length < 128 && BROWSER_ID_PATTERN.test(value);
}

function resolveStorage(storage?: BrowserIdStorage): BrowserIdStorage {
  if (storage && typeof storage.getItem === "function" && typeof storage.setItem === "function") {
    return storage;
  }

  if (typeof window !== "undefined") {
    const candidate = window.localStorage;

    if (candidate && typeof candidate.getItem === "function" && typeof candidate.setItem === "function") {
      return candidate;
    }
  }

  return {
    getItem(key) {
      return fallbackValues.get(key) ?? null;
    },
    setItem(key, value) {
      fallbackValues.set(key, value);
    },
  };
}

export function getBrowserId(storage?: BrowserIdStorage): string {
  const resolvedStorage = resolveStorage(storage);
  const existing = resolvedStorage.getItem(BROWSER_ID_KEY);

  if (isValidBrowserId(existing)) {
    return existing;
  }

  const next = makeBrowserId();

  resolvedStorage.setItem(BROWSER_ID_KEY, next);
  return next;
}
