export type Position = { line: number; character: number };
export type Range = { start: Position; end: Position };
export type VirtualFile = { path: string; source: string };
export type VirtualSource = { path: string; source: string };

export interface BrowserWorkspace {
  entry_path: string;
  source: string;
  files: VirtualFile[];
  include_directories?: string[];
  imports?: string[];
  shell?: VirtualSource;
  engine?: VirtualSource;
  data?: Record<string, unknown>;
  context_schema?: Record<string, unknown>;
  output_context?: "html" | "subject";
}

export interface Diagnostic {
  code: string;
  severity: "error" | "warning" | "information";
  message: string;
  path?: string;
  range?: Range;
}

export interface SymbolResult {
  name: string;
  kind: string;
  range: Range;
}

export interface AnalyzeResult {
  success: boolean;
  authoritative: false;
  output_kind: "final-html" | "engine-template" | "engine-definition";
  diagnostics: Diagnostic[];
  dependencies: string[];
  symbols: SymbolResult[];
  preview:
    | { kind: "final-html" | "sample-html"; html: string }
    | { kind: "target-source"; source: string }
    | null;
}

export interface CompletionResult {
  is_incomplete: boolean;
  items: Array<{
    label: string;
    kind: string;
    insert_text: string;
    detail?: string;
    documentation?: string;
    replace?: Range;
  }>;
}

interface Response<T> {
  protocol: "email-markup.browser";
  version: 1;
  compiler_version: string | null;
  id: number | null;
  ok: boolean;
  result?: T;
  error?: { code: string; message: string };
}

interface Pending {
  resolve(value: unknown): void;
  reject(reason: Error): void;
  timer: ReturnType<typeof setTimeout>;
}

export class BrowserCompiler {
  private readonly worker: Worker;
  private readonly pending = new Map<number, Pending>();
  private failure: Error | undefined;
  private sequence = 0;

  constructor(workerUrl: string) {
    this.worker = new Worker(workerUrl, { name: "email-markup-compiler" });
    this.worker.addEventListener("message", this.receive);
    this.worker.addEventListener("error", this.failWorker);
    this.worker.addEventListener("messageerror", this.failWorker);
  }

  analyze(workspace: BrowserWorkspace): Promise<AnalyzeResult> {
    return this.request("analyze", workspace);
  }

  format(path: string, source: string): Promise<{ text: string; changed: boolean }> {
    return this.request("format", { path, source });
  }

  complete(workspace: BrowserWorkspace, position: Position): Promise<CompletionResult> {
    return this.request("complete", { ...workspace, position });
  }

  hover(workspace: BrowserWorkspace, position: Position): Promise<{ markdown: string } | null> {
    return this.request("hover", { ...workspace, position });
  }

  signature(
    workspace: BrowserWorkspace,
    position: Position,
  ): Promise<{
    label: string;
    parameters: Array<{ label: string }>;
    active_parameter: number;
  } | null> {
    return this.request("signature", { ...workspace, position });
  }

  dispose(): void {
    this.worker.terminate();
    this.rejectAll(new Error("Email Markup browser compiler stopped."));
  }

  private request<T>(method: string, params: object): Promise<T> {
    if (this.failure) return Promise.reject(this.failure);
    const id = ++this.sequence;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error("Email Markup browser compiler timed out."));
      }, 10_000);
      this.pending.set(id, { resolve: (value) => resolve(value as T), reject, timer });
      this.worker.postMessage({
        protocol: "email-markup.browser",
        version: 1,
        id,
        method,
        params,
      });
    });
  }

  private readonly receive = (event: MessageEvent<Response<unknown>>): void => {
    const response = event.data;
    if (!response || response.protocol !== "email-markup.browser" || response.version !== 1) {
      this.failWorker();
      return;
    }
    if (typeof response.id !== "number") return;
    const pending = this.pending.get(response.id);
    if (!pending) return;
    clearTimeout(pending.timer);
    this.pending.delete(response.id);
    if (response.ok) pending.resolve(response.result);
    else pending.reject(new Error(response.error?.message ?? "Browser compiler request failed."));
  };

  private readonly failWorker = (): void => {
    this.failure = new Error("Email Markup browser compiler is unavailable.");
    this.rejectAll(this.failure);
  };

  private rejectAll(error: Error): void {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
  }
}
