export type SessionResponse = {
  paired: boolean;
  csrf: string | null;
  trustedCount: number;
  capabilities: {
    terminal: boolean;
  };
};

export type StatusResponse = {
  platform: string;
  port: number;
  trustedCount: number;
};

export type BiosSummary = {
  required: number;
  present: number;
  satisfied: boolean;
};

export type BiosRequirement = {
  label: string;
  fileName: string;
  path: string;
  status: string;
  required: boolean;
};

export type PlatformSummary = {
  tag: string;
  name: string;
  group: string;
  icon: string;
  isCustom: boolean;
  romPath: string;
  savePath: string;
  biosPath: string;
  counts: {
    roms: number;
    saves: number;
    bios: number;
    overlays: number;
    cheats: number;
  };
  bios: BiosSummary;
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
  biosSummary?: BiosSummary;
  biosRequirements?: BiosRequirement[];
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

export type ToolKey = "file-browser" | "logs" | "terminal" | "collections" | "screenshots";

export type LogFileSummary = {
  path: string;
  size: number;
  modified: number;
};

export type LogsResponse = {
  root: string;
  files: LogFileSummary[];
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
