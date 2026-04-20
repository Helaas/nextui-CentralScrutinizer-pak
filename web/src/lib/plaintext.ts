const PLAINTEXT_EXTENSIONS = new Set([
  "cfg",
  "conf",
  "csv",
  "ini",
  "json",
  "log",
  "lua",
  "md",
  "properties",
  "sh",
  "toml",
  "tsv",
  "txt",
  "xml",
  "yaml",
  "yml",
]);

export const PLAINTEXT_MAX_BYTES = 1 << 20;

export function isPlaintextFileName(name: string): boolean {
  const dot = name.lastIndexOf(".");

  if (dot <= 0 || dot === name.length - 1) {
    return false;
  }

  return PLAINTEXT_EXTENSIONS.has(name.slice(dot + 1).toLowerCase());
}
