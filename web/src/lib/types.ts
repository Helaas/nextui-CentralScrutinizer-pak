export type SessionResponse = {
  paired: boolean;
  csrf: string | null;
  trustedCount: number;
  pairingAvailable: boolean;
  capabilities: {
    terminal: boolean;
  };
};

export type StatusResponse = {
  platform: string;
  port: number;
  trustedCount: number;
};

export type PlatformResource = "roms" | "saves" | "states" | "bios" | "overlays" | "cheats";
export type LibraryEmuFilter = "all" | "installed";

export type SupportedResources = Record<PlatformResource, boolean>;

export type PlatformSummary = {
  tag: string;
  name: string;
  group: string;
  icon: string;
  isCustom: boolean;
  requiresEmulator: boolean;
  emulatorInstalled: boolean;
  emulatorWarning: string | null;
  romPath: string;
  savePath: string;
  biosPath: string;
  supportedResources: SupportedResources;
  counts: {
    roms: number;
    saves: number;
    states: number;
    bios: number;
    overlays: number;
    cheats: number;
  };
};

export type PlatformGroup = {
  name: string;
  platforms: PlatformSummary[];
};

export type PlatformsResponse = {
  groups: PlatformGroup[];
};

export type BrowserScope = "roms" | "saves" | "bios" | "overlays" | "cheats" | "files";

export type BrowserEntry = {
  name: string;
  path: string;
  type: string;
  size: number;
  modified: number;
  status: string;
  thumbnailPath: string;
};

export type Breadcrumb = {
  label: string;
  path: string;
};

export type BrowserResponse = {
  scope: BrowserScope;
  title: string;
  rootPath: string;
  path: string;
  breadcrumbs: Breadcrumb[];
  entries: BrowserEntry[];
  truncated: boolean;
};

export type SaveStateEntry = {
  id: string;
  title: string;
  coreDir: string;
  slot: number;
  slotLabel: string;
  kind: string;
  format: string;
  modified: number;
  size: number;
  previewPath: string;
  downloadPaths: string[];
  deletePaths: string[];
  warnings: string[];
};

export type SaveStatesResponse = {
  platformTag: string;
  platformName: string;
  emuCode: string;
  count: number;
  truncated: boolean;
  entries: SaveStateEntry[];
};

export type UploadRequest = {
  scope: BrowserScope;
  tag?: string;
  path?: string;
  files: File[];
};

export type ReplaceArtRequest = {
  tag: string;
  path: string;
  file: File;
};

export type MutationRequest = {
  scope: BrowserScope;
  tag?: string;
  path: string;
};

export type RenameRequest = {
  scope: BrowserScope;
  tag?: string;
  from: string;
  to: string;
};

export type WriteRequest = {
  scope: BrowserScope;
  tag?: string;
  path: string;
  content: string;
};

export type ToolKey =
  | "file-browser"
  | "logs"
  | "terminal"
  | "collections"
  | "screenshots"
  | "mac-dot-clean";

export type LogFileSummary = {
  path: string;
  size: number;
  modified: number;
};

export type LogsResponse = {
  root: string;
  files: LogFileSummary[];
};

export type MacDotfileEntry = {
  path: string;
  kind: string;
  reason: string;
  size: number;
  modified: number;
};

export type MacDotfilesResponse = {
  count: number;
  truncated: boolean;
  entries: MacDotfileEntry[];
};

export type TerminalSessionResponse = {
  ticket: string;
  expiresIn: number;
};

export type FileSearchResult = {
  path: string;
  type: string;
};

export type FileSearchResponse = {
  basePath: string;
  query: string;
  results: FileSearchResult[];
  truncated: boolean;
};

export type TransferState = {
  active: boolean;
  label: string;
  progress: number;
  cancellable?: boolean;
  onCancel?: () => void;
};
