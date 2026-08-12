import * as vscode from "vscode";

import {
  BrowserCompiler,
  type BrowserWorkspace,
  type Position,
  type Range,
} from "./browserClient";

const refreshDelayMs = 180;
const supportedPath = /\.(em|emt)$/u;

function compilerPath(document: vscode.TextDocument): string {
  const relative = vscode.workspace.asRelativePath(document.uri, false);
  return `/${relative.replace(/^\/+/, "")}`;
}

function compilerPathForUri(uri: vscode.Uri): string {
  return `/${vscode.workspace.asRelativePath(uri, false).replace(/^\/+/, "")}`;
}

function compilerPosition(position: vscode.Position): Position {
  return { line: position.line, character: position.character };
}

function vscodeRange(range: Range): vscode.Range {
  return new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character,
  );
}

function diagnosticSeverity(value: string): vscode.DiagnosticSeverity {
  if (value === "warning") return vscode.DiagnosticSeverity.Warning;
  if (value === "information") return vscode.DiagnosticSeverity.Information;
  return vscode.DiagnosticSeverity.Error;
}

function completionKind(value: string): vscode.CompletionItemKind {
  if (value === "component") return vscode.CompletionItemKind.Class;
  if (value === "property") return vscode.CompletionItemKind.Property;
  if (value === "type") return vscode.CompletionItemKind.TypeParameter;
  if (value === "module") return vscode.CompletionItemKind.Module;
  if (value === "value") return vscode.CompletionItemKind.Value;
  if (value === "snippet") return vscode.CompletionItemKind.Snippet;
  if (value === "template") return vscode.CompletionItemKind.Function;
  return vscode.CompletionItemKind.Keyword;
}

function symbolKind(value: string): vscode.SymbolKind {
  if (value === "component") return vscode.SymbolKind.Class;
  if (value === "style") return vscode.SymbolKind.Object;
  if (value === "token") return vscode.SymbolKind.Constant;
  if (value === "template") return vscode.SymbolKind.Function;
  return vscode.SymbolKind.Variable;
}

function previewHtml(result: Awaited<ReturnType<BrowserCompiler["analyze"]>>): string | null {
  const preview = result.preview;
  if (!preview) return null;
  if (preview.kind === "target-source") {
    const escaped = preview.source
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;");
    return `<main style="font:14px/1.5 ui-monospace,monospace;padding:24px;color:#1a1523"><h1 style="font:600 18px/1.4 system-ui,sans-serif">Live preview cannot execute deferred target source</h1><p style="font-family:system-ui,sans-serif">Use Verified preview for Django rendering and publication evidence.</p><pre style="white-space:pre-wrap;overflow-wrap:anywhere">${escaped}</pre></main>`;
  }
  return preview.html;
}

function previewDocument(body: string): string {
  const escaped = body.replaceAll("&", "&amp;").replaceAll('"', "&quot;");
  return `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'"><style>body{margin:0;background:#fff}.state{padding:8px 12px;background:#f5f3f9;color:#322b3f;font:600 12px/1.4 system-ui,sans-serif;border-bottom:1px solid #e7e3ef}iframe{border:0;width:100%;height:calc(100vh - 34px)}</style></head><body><div class="state">Live · browser compiler · not publication proof</div><iframe sandbox srcdoc="${escaped}"></iframe></body></html>`;
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const output = vscode.window.createOutputChannel("Email Markup", { log: true });
  const diagnostics = vscode.languages.createDiagnosticCollection("email-markup");
  const workerUrl = vscode.Uri.joinPath(
    context.extensionUri,
    "browser",
    "email-markup.worker.mjs",
  ).toString(true);
  const compiler = new BrowserCompiler(workerUrl);
  const files = new Map<string, string>();
  const refreshTimers = new Map<string, ReturnType<typeof setTimeout>>();
  let previewPanel: vscode.WebviewPanel | undefined;
  let previewDocumentUri = "";
  let previewRequest = 0;

  context.subscriptions.push(output, diagnostics, compiler);

  async function loadProject(): Promise<void> {
    const uris = await vscode.workspace.findFiles("**/*", "**/{generated,node_modules}/**", 2000);
    await Promise.all(
      uris
        .filter((uri) => /\.(em|emt|css|json)$/u.test(uri.path))
        .map(async (uri) => {
          try {
            const source = new TextDecoder("utf-8", { fatal: true }).decode(
              await vscode.workspace.fs.readFile(uri),
            );
            files.set(compilerPathForUri(uri), source);
          } catch (error) {
            output.warn(`Skipped ${uri.toString(true)}: ${String(error)}`);
          }
        }),
    );
    output.info(`Loaded ${files.size} bounded virtual project files.`);
  }

  function workspaceFor(document: vscode.TextDocument): BrowserWorkspace {
    const entryPath = compilerPath(document);
    const source = document.getText();
    files.set(entryPath, source);
    return {
      entry_path: entryPath,
      source,
      files: [...files.entries()]
        .filter(([path]) => path !== entryPath)
        .map(([path, fileSource]) => ({ path, source: fileSource })),
      include_directories: ["/components", "/shells", "/styles", "/templates"],
    };
  }

  async function analyze(document: vscode.TextDocument): Promise<void> {
    if (document.languageId !== "email-markup") return;
    const version = document.version;
    try {
      const result = await compiler.analyze(workspaceFor(document));
      if (document.isClosed || document.version !== version) return;
      diagnostics.set(
        document.uri,
        result.diagnostics
          .filter((item) => !item.path || item.path === compilerPath(document))
          .map((item) => {
            const diagnostic = new vscode.Diagnostic(
              item.range ? vscodeRange(item.range) : new vscode.Range(0, 0, 0, 1),
              item.message,
              diagnosticSeverity(item.severity),
            );
            diagnostic.code = item.code;
            diagnostic.source = "Email Markup browser compiler";
            return diagnostic;
          }),
      );
      if (previewPanel && previewDocumentUri === document.uri.toString()) {
        const request = ++previewRequest;
        const body = previewHtml(result);
        if (request === previewRequest && body !== null) {
          previewPanel.webview.html = previewDocument(body);
        } else if (request === previewRequest) {
          previewPanel.title = "Email Markup Live preview · stale";
        }
      }
    } catch (error) {
      output.error(`Analysis failed for ${document.uri.toString(true)}.`, error);
    }
  }

  await loadProject();
  for (const document of vscode.workspace.textDocuments) void analyze(document);

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => void analyze(document)),
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.languageId !== "email-markup") return;
      const key = event.document.uri.toString();
      const active = refreshTimers.get(key);
      if (active) clearTimeout(active);
      refreshTimers.set(
        key,
        setTimeout(() => {
          refreshTimers.delete(key);
          void analyze(event.document);
        }, refreshDelayMs),
      );
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      diagnostics.delete(document.uri);
      const timer = refreshTimers.get(document.uri.toString());
      if (timer) clearTimeout(timer);
      refreshTimers.delete(document.uri.toString());
    }),
    vscode.languages.registerCompletionItemProvider(
      [{ language: "email-markup", scheme: "email-content" }, { language: "email-markup", scheme: "file" }],
      {
        async provideCompletionItems(document, position) {
          const version = document.version;
          const result = await compiler.complete(workspaceFor(document), compilerPosition(position));
          if (document.version !== version) return [];
          return result.items.map((item) => {
            const completion = new vscode.CompletionItem(item.label, completionKind(item.kind));
            completion.insertText = item.insert_text;
            completion.detail = item.detail;
            completion.documentation = item.documentation;
            if (item.replace) completion.range = vscodeRange(item.replace);
            return completion;
          });
        },
      },
      "@", "{", ".", ":", ",",
    ),
    vscode.languages.registerHoverProvider("email-markup", {
      async provideHover(document, position) {
        const version = document.version;
        const result = await compiler.hover(workspaceFor(document), compilerPosition(position));
        if (!result || document.version !== version) return null;
        return new vscode.Hover(new vscode.MarkdownString(result.markdown));
      },
    }),
    vscode.languages.registerSignatureHelpProvider(
      "email-markup",
      {
        async provideSignatureHelp(document, position) {
          const version = document.version;
          const result = await compiler.signature(workspaceFor(document), compilerPosition(position));
          if (!result || document.version !== version) return null;
          const help = new vscode.SignatureHelp();
          const signature = new vscode.SignatureInformation(result.label);
          signature.parameters = result.parameters.map(
            (parameter) => new vscode.ParameterInformation(parameter.label),
          );
          help.signatures = [signature];
          help.activeSignature = 0;
          help.activeParameter = result.active_parameter;
          return help;
        },
      },
      { triggerCharacters: ["(", ","], retriggerCharacters: [","] },
    ),
    vscode.languages.registerDocumentFormattingEditProvider("email-markup", {
      async provideDocumentFormattingEdits(document) {
        const version = document.version;
        const result = await compiler.format(compilerPath(document), document.getText());
        if (!result.changed || document.version !== version) return [];
        const last = document.lineAt(document.lineCount - 1);
        return [vscode.TextEdit.replace(new vscode.Range(0, 0, last.lineNumber, last.text.length), result.text)];
      },
    }),
    vscode.languages.registerDocumentSymbolProvider("email-markup", {
      async provideDocumentSymbols(document) {
        const version = document.version;
        const result = await compiler.analyze(workspaceFor(document));
        if (document.version !== version) return [];
        return result.symbols.map(
          (symbol) => new vscode.DocumentSymbol(
            symbol.name,
            symbol.kind,
            symbolKind(symbol.kind),
            vscodeRange(symbol.range),
            vscodeRange(symbol.range),
          ),
        );
      },
    }),
    vscode.commands.registerCommand("email-markup.preview", async () => {
      const document = vscode.window.activeTextEditor?.document;
      if (!document || document.languageId !== "email-markup") return;
      const result = await compiler.analyze(workspaceFor(document));
      const body = previewHtml(result);
      if (body === null) {
        void vscode.window.showWarningMessage("Live preview is unavailable because the document has compiler errors.");
        return;
      }
      if (!previewPanel) {
        previewPanel = vscode.window.createWebviewPanel(
          "emailMarkupLivePreview",
          "Email Markup Live preview",
          vscode.ViewColumn.Beside,
          { enableScripts: false, localResourceRoots: [] },
        );
        previewPanel.onDidDispose(() => {
          previewPanel = undefined;
          previewDocumentUri = "";
          ++previewRequest;
        });
      }
      previewDocumentUri = document.uri.toString();
      previewPanel.webview.html = previewDocument(body);
      previewPanel.reveal(vscode.ViewColumn.Beside, true);
    }),
  );
}

export function deactivate(): void {}
