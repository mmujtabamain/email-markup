export type Position = { line: number; character: number };
export type VirtualFile = { path: string; source: string };

export type BrowserWorkspace = {
  entry_path: string;
  source: string;
  files?: VirtualFile[];
  include_directories?: string[];
  imports?: string[];
  shell?: VirtualFile;
  engine?: VirtualFile;
  data?: Record<string, unknown>;
  output_context?: "html" | "subject";
};

export type BrowserMethod =
  | "capabilities"
  | "analyze"
  | "format"
  | "complete"
  | "hover"
  | "signature";

export type BrowserRequest = {
  protocol: "email-markup.browser";
  version: 1;
  id: string | number;
  method: BrowserMethod;
  params: Record<string, unknown>;
};

export type BrowserError = {
  protocol: "email-markup.browser";
  version: 1;
  compiler_version: string | null;
  id: string | number | null;
  ok: false;
  error: { code: string; message: string };
};

export type BrowserSuccess<T = unknown> = {
  protocol: "email-markup.browser";
  version: 1;
  compiler_version: string;
  id: string | number;
  ok: true;
  result: T;
};

export type BrowserResponse<T = unknown> = BrowserSuccess<T> | BrowserError;
