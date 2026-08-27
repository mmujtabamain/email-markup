import * as vscode from "vscode";

import { registerWebFeatures } from "../webFeatures";
import { BrowserCompiler, type AnalyzeResult, type Position, type Range } from "./browserClient";
import { readDefinitions } from "./definitions";
import { registerJsonEditors } from "./jsonEditors";
import {
  EmailMarkupProject,
  compilerPathForUri,
  outputContextFor,
  parentPath,
  type BuiltWorkspace,
} from "./project";
import {
  galleryEntry,
  planPreview,
  renderBlocked,
  renderHtmlPreview,
  renderNothingToRender,
  renderSubjectPreview,
  renderTargetSource,
  shellBodyEntry,
  syntheticEntryName,
  tokenEntry,
} from "./preview";
import {
  EmailMarkupSemanticTokensProvider,
  semanticTokenLegend,
  type ProjectVocabulary,
} from "./semanticTokens";

const refreshDelayMs = 180;

function compilerPath(document: vscode.TextDocument): string {
  return compilerPathForUri(document.uri);
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

interface Lexical {
  keywords?: string[];
  propTypes?: string[];
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const output = vscode.window.createOutputChannel("Email Markup", { log: true });
  const diagnostics = vscode.languages.createDiagnosticCollection("email-markup");
  const workerUrl = vscode.Uri.joinPath(
    context.extensionUri,
    "browser",
    "email-markup.worker.mjs",
  ).toString(true);
  const compiler = new BrowserCompiler(workerUrl, (message) => output.warn(message));
  const project = new EmailMarkupProject(context.extensionUri, output);
  const refreshTimers = new Map<string, ReturnType<typeof setTimeout>>();
  let previewPanel: vscode.WebviewPanel | undefined;
  let previewDocumentUri = "";
  let previewRequest = 0;

  context.subscriptions.push(output, diagnostics, compiler, project);

  function reportError(contextLabel: string, error: unknown, document?: vscode.TextDocument): void {
    const message = error instanceof Error ? error.message : String(error);
    const stack = error instanceof Error && error.stack ? error.stack : message;
    output.error(`${contextLabel}: ${stack}`);
    output.show(true);
    if (document) {
      const diagnostic = new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        `${message} — the full call stack is in the Email Markup output.`,
        vscode.DiagnosticSeverity.Error,
      );
      diagnostic.code = "browser_worker_failure";
      diagnostic.source = "Email Markup browser compiler";
      diagnostics.set(document.uri, [diagnostic]);
    }
    void vscode.window.showErrorMessage(
      `${contextLabel}: ${message}. The full call stack is in the Email Markup output.`,
    );
  }

  /*
   * There were global `error` and `unhandledrejection` handlers here. The web
   * extension host runs in a worker that refuses them — every activation logged
   * "'addEventListener' has been blocked", several times over, and the handlers
   * never ran. Errors from this extension already reach the Email Markup output
   * channel through `reportError`, and anything that escapes reaches the
   * extension host's own reporting; the listeners added noise and nothing else.
   */

  // ---------------------------------------------------------------- vocabulary

  let lexical: Lexical = {};
  try {
    const response = await fetch(
      vscode.Uri.joinPath(context.extensionUri, "browser", "syntax", "lexical.json").toString(true),
    );
    if (response.ok) lexical = (await response.json()) as Lexical;
  } catch (error) {
    output.warn(`Lexical description unavailable, semantic highlighting reduced: ${String(error)}`);
  }

  let vocabularyRevision = -1;
  let vocabularyValue: ProjectVocabulary = {
    keywords: new Set(lexical.keywords ?? []),
    propTypes: new Set(lexical.propTypes ?? []),
    components: new Set<string>(),
    tokens: new Set<string>(),
  };

  /** Component and token names across the whole project, recomputed only when it changes. */
  function vocabulary(): ProjectVocabulary {
    if (project.revision === vocabularyRevision) return vocabularyValue;
    const components = new Set<string>();
    const tokens = new Set<string>();
    for (const source of project.sources().values()) {
      const definitions = readDefinitions(source);
      for (const component of definitions.components) components.add(component.name);
      for (const token of definitions.tokens) tokens.add(token.name);
    }
    vocabularyRevision = project.revision;
    vocabularyValue = {
      keywords: new Set(lexical.keywords ?? []),
      propTypes: new Set(lexical.propTypes ?? []),
      components,
      tokens,
    };
    return vocabularyValue;
  }

  // ---------------------------------------------------------------- analysis

  const semanticTokens = new EmailMarkupSemanticTokensProvider(vocabulary);
  context.subscriptions.push(semanticTokens);

  function build(document: vscode.TextDocument): BuiltWorkspace {
    return project.build(compilerPath(document), document.getText());
  }

  async function analyze(document: vscode.TextDocument): Promise<void> {
    if (document.languageId !== "email-markup") return;
    const version = document.version;
    const path = compilerPath(document);
    try {
      const built = build(document);
      const result = await compiler.analyze(built.workspace);
      if (document.isClosed || document.version !== version) return;
      diagnostics.set(
        document.uri,
        result.diagnostics
          .filter((item) => !item.path || item.path === path)
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
        await showPreview(document, built, result);
      }
    } catch (error) {
      reportError(`Analysis failed for ${document.uri.toString(true)}`, error, document);
    }
  }

  function analyzeOpenDocuments(): void {
    for (const document of vscode.workspace.textDocuments) void analyze(document);
  }

  // ---------------------------------------------------------------- preview

  /**
   * Render whatever this document's preview should be.
   *
   * A component library, a token sheet and a shell all compile to nothing on
   * their own, so each is previewed by compiling a small synthetic document that
   * exercises what the open file defines. The synthetic entry lives beside the
   * real one and pulls it in through `imports`, so it sees the live buffer rather
   * than what is on disk.
   */
  async function showPreview(
    document: vscode.TextDocument,
    built: BuiltWorkspace,
    result: AnalyzeResult,
  ): Promise<void> {
    if (!previewPanel) return;
    const request = ++previewRequest;
    const path = compilerPath(document);
    const definitions = readDefinitions(document.getText());
    const plan = planPreview(path, result, definitions, built.roles, outputContextFor(path));

    const syntheticPath = `${parentPath(path)}/${syntheticEntryName}`;
    const compileSynthetic = async (
      source: string,
      options: Parameters<typeof project.build>[2],
    ): Promise<AnalyzeResult> =>
      compiler.analyze(project.build(syntheticPath, source, options).workspace);

    let html: string;
    try {
      switch (plan.kind) {
        case "document": {
          const preview = result.preview;
          if (!preview) return;
          if (preview.kind === "target-source") {
            html = renderTargetSource(preview.source);
          } else if (plan.outputContext === "subject") {
            html = renderSubjectPreview(preview.html);
          } else {
            html = renderHtmlPreview("Message preview", preview.html);
          }
          break;
        }
        case "shell": {
          const synthetic = await compileSynthetic(shellBodyEntry(), { shellPath: path });
          html =
            synthetic.preview && synthetic.preview.kind !== "target-source"
              ? renderHtmlPreview("Shell · with placeholder content", synthetic.preview.html)
              : renderNothingToRender(
                  "The shell could not be previewed",
                  "A placeholder body was compiled against this shell and produced no output. The Problems panel lists anything the compiler reported.",
                );
          break;
        }
        case "gallery": {
          const synthetic = await compileSynthetic(galleryEntry(plan.components), {
            extraImports: [path],
          });
          html =
            synthetic.preview && synthetic.preview.kind !== "target-source"
              ? renderHtmlPreview(
                  `Component gallery · ${plan.components.length} component${
                    plan.components.length === 1 ? "" : "s"
                  }`,
                  synthetic.preview.html,
                )
              : renderNothingToRender(
                  "The components could not be rendered",
                  "Each component was instantiated with placeholder props and slots, and the compiler produced no output. The Problems panel lists anything it reported.",
                );
          break;
        }
        case "tokens": {
          const synthetic = await compileSynthetic(tokenEntry(plan.tokens), {
            extraImports: [path],
          });
          html =
            synthetic.preview && synthetic.preview.kind !== "target-source"
              ? renderHtmlPreview(
                  `Design tokens · ${plan.tokens.length} token${
                    plan.tokens.length === 1 ? "" : "s"
                  }`,
                  synthetic.preview.html,
                )
              : renderNothingToRender(
                  "The tokens could not be rendered",
                  "A swatch sheet was compiled from this file and produced no output. The Problems panel lists anything the compiler reported.",
                );
          break;
        }
        case "blocked":
          html = renderBlocked(plan.errors);
          break;
        default:
          html = renderNothingToRender(plan.headline, plan.detail);
      }
    } catch (error) {
      output.error(`Preview failed for ${path}: ${String(error)}`);
      html = renderNothingToRender(
        "The preview could not be produced",
        error instanceof Error ? error.message : String(error),
      );
    }

    if (request !== previewRequest || !previewPanel) return;
    previewPanel.webview.html = html;
  }

  async function openPreview(): Promise<void> {
    const document = vscode.window.activeTextEditor?.document;
    if (!document || document.languageId !== "email-markup") return;
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
    previewPanel.reveal(vscode.ViewColumn.Beside, true);
    try {
      const built = build(document);
      await showPreview(document, built, await compiler.analyze(built.workspace));
    } catch (error) {
      reportError(`Preview failed for ${document.uri.toString(true)}`, error, document);
    }
  }

  // ---------------------------------------------------------------- wiring

  /*
   * Everything is registered before the project is read.
   *
   * Activation used to await `project.load()` first, which walks the workspace
   * and reads every source over the network. Any surface that activates this
   * extension — opening `em.json`, opening a template — therefore waited for the
   * whole repository before its provider existed, so a custom editor appeared to
   * hang on a document that was already in memory. The providers work against an
   * empty project and simply get better once it arrives.
   */
  registerWebFeatures(context);

  context.subscriptions.push(
    ...registerJsonEditors(),
    project.onDidChange(() => {
      semanticTokens.refresh();
      analyzeOpenDocuments();
    }),
    vscode.workspace.onDidOpenTextDocument((document) => void analyze(document)),
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.languageId !== "email-markup") {
        const path = compilerPath(event.document);
        if (project.hasJson(path) || path.endsWith(".json")) {
          project.setJson(path, event.document.getText());
          analyzeOpenDocuments();
        }
        return;
      }
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
    vscode.languages.registerDocumentSemanticTokensProvider(
      { language: "email-markup" },
      semanticTokens,
      semanticTokenLegend,
    ),
    vscode.languages.registerCompletionItemProvider(
      { language: "email-markup" },
      {
        async provideCompletionItems(document, position) {
          const version = document.version;
          const result = await compiler.complete(
            build(document).workspace,
            compilerPosition(position),
          );
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
    vscode.languages.registerHoverProvider(
      { language: "email-markup" },
      {
        async provideHover(document, position) {
          const version = document.version;
          const result = await compiler.hover(
            build(document).workspace,
            compilerPosition(position),
          );
          if (!result || document.version !== version) return null;
          return new vscode.Hover(new vscode.MarkdownString(result.markdown));
        },
      },
    ),
    vscode.languages.registerSignatureHelpProvider(
      { language: "email-markup" },
      {
        async provideSignatureHelp(document, position) {
          const version = document.version;
          const result = await compiler.signature(
            build(document).workspace,
            compilerPosition(position),
          );
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
    vscode.languages.registerDocumentFormattingEditProvider(
      { language: "email-markup" },
      {
        async provideDocumentFormattingEdits(document) {
          const version = document.version;
          const result = await compiler.format(compilerPath(document), document.getText());
          if (!result.changed || document.version !== version) return [];
          const last = document.lineAt(document.lineCount - 1);
          return [
            vscode.TextEdit.replace(
              new vscode.Range(0, 0, last.lineNumber, last.text.length),
              result.text,
            ),
          ];
        },
      },
    ),
    vscode.languages.registerDocumentSymbolProvider(
      { language: "email-markup" },
      {
        async provideDocumentSymbols(document) {
          const version = document.version;
          const result = await compiler.analyze(build(document).workspace);
          if (document.version !== version) return [];
          return result.symbols.map(
            (symbol) =>
              new vscode.DocumentSymbol(
                symbol.name,
                symbol.kind,
                symbolKind(symbol.kind),
                vscodeRange(symbol.range),
                vscodeRange(symbol.range),
              ),
          );
        },
      },
    ),
    vscode.commands.registerCommand("email-markup.preview", () => void openPreview()),
  );

  // Reading the project is what takes time, so it happens after everything is
  // registered rather than in front of it.
  try {
    await project.load();
    project.watch();
    semanticTokens.refresh();
    analyzeOpenDocuments();
  } catch (error) {
    reportError("The Email Markup project could not be read", error);
  }
}

export function deactivate(): void {}
