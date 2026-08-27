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
  error?: { code: string; message: string; stack?: string };
}

interface Pending {
  resolve(value: unknown): void;
  reject(reason: Error): void;
  timer: ReturnType<typeof setTimeout>;
}

/**
 * The compiler runs in a worker, and a worker can die — a malformed project, a
 * Wasm abort, a memory limit. Treating that as terminal used to end the session:
 * one failure latched a flag that every later request checked, so completions,
 * hover, formatting and analysis all stayed broken until the page was reloaded,
 * and reloading the hosted editor drops the author out of it entirely.
 *
 * So a failure retires the worker rather than the client. The next request
 * starts a fresh one, and a short backoff — growing only while failures keep
 * arriving close together — stops a reproducibly-crashing document from
 * spinning the worker in a loop.
 */
export class BrowserCompiler {
  private worker: Worker | undefined;
  private readonly pending = new Map<number, Pending>();
  private sequence = 0;
  private disposed = false;
  private failureCount = 0;
  private lastFailureAt = 0;
  private retryAt = 0;

  constructor(
    private readonly workerUrl: string,
    private readonly warn: (message: string) => void = () => {},
  ) {}

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
    this.disposed = true;
    this.retire(new Error("Email Markup browser compiler stopped."));
  }

  private ensureWorker(): Worker {
    if (this.worker) return this.worker;
    const worker = new Worker(this.workerUrl, { name: "email-markup-compiler" });
    worker.addEventListener("message", this.receive);
    worker.addEventListener("error", this.onWorkerError);
    worker.addEventListener("messageerror", this.onWorkerError);
    this.worker = worker;
    return worker;
  }

  /** Stop the current worker and fail everything still waiting on it. */
  private retire(reason: Error): void {
    const worker = this.worker;
    this.worker = undefined;
    if (worker) {
      worker.removeEventListener("message", this.receive);
      worker.removeEventListener("error", this.onWorkerError);
      worker.removeEventListener("messageerror", this.onWorkerError);
      worker.terminate();
    }
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(reason);
    }
    this.pending.clear();
  }

  private readonly onWorkerError = (event?: Event): void => {
    const reason =
      event instanceof ErrorEvent && event.error instanceof Error
        ? event.error
        : new Error(
            event instanceof ErrorEvent && event.message
              ? event.message
              : "Email Markup browser compiler stopped responding.",
          );
    this.failed(reason);
  };

  private failed(reason: Error): void {
    const now = Date.now();
    // Only treat failures as consecutive while they keep arriving close together;
    // an isolated crash an hour later should not inherit an old backoff.
    this.failureCount = now - this.lastFailureAt < 10_000 ? this.failureCount + 1 : 1;
    this.lastFailureAt = now;
    const backoff = Math.min(5_000, [0, 0, 500, 2_000][this.failureCount] ?? 5_000);
    this.retryAt = now + backoff;
    this.retire(reason);
    this.warn(
      `Email Markup browser compiler restarted after a failure (${reason.message}). ` +
        (backoff ? `Waiting ${backoff}ms before the next attempt.` : "Retrying on the next request."),
    );
  }

  private request<T>(method: string, params: object): Promise<T> {
    if (this.disposed) {
      return Promise.reject(new Error("Email Markup browser compiler stopped."));
    }
    const wait = this.retryAt - Date.now();
    if (wait > 0) {
      return Promise.reject(
        new Error(
          `Email Markup browser compiler is restarting after a failure; retrying in ${wait}ms.`,
        ),
      );
    }
    const id = ++this.sequence;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error("Email Markup browser compiler timed out."));
        // The worker is still busy with the request that timed out and will stay
        // behind for every later one, so it is retired rather than reused.
        this.failed(new Error("Email Markup browser compiler timed out."));
      }, 10_000);
      this.pending.set(id, { resolve: (value) => resolve(value as T), reject, timer });
      try {
        this.ensureWorker().postMessage({
          protocol: "email-markup.browser",
          version: 1,
          id,
          method,
          params,
        });
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        const reason = error instanceof Error ? error : new Error(String(error));
        this.failed(reason);
        reject(reason);
      }
    });
  }

  private readonly receive = (event: MessageEvent<Response<unknown>>): void => {
    const response = event.data;
    if (!response || response.protocol !== "email-markup.browser" || response.version !== 1) {
      this.failed(new Error("Email Markup browser compiler sent an unrecognized response."));
      return;
    }
    if (typeof response.id !== "number") return;
    const pending = this.pending.get(response.id);
    if (!pending) return;
    clearTimeout(pending.timer);
    this.pending.delete(response.id);
    // A response of any shape means the worker is alive and serving requests.
    this.failureCount = 0;
    if (response.ok) {
      pending.resolve(response.result);
      return;
    }
    const error = new Error(response.error?.message ?? "Browser compiler request failed.");
    if (response.error?.stack) error.stack = response.error.stack;
    pending.reject(error);
  };
}
