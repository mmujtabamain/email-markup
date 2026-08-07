import * as path from "node:path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

import { previewDocument } from "./preview";
import { registerWebFeatures } from "./webFeatures";

const protocolVersion = 1;
const previewRefreshDelayMs = 200;

let client: LanguageClient | undefined;
let output: vscode.LogOutputChannel | undefined;
let previewPanel: vscode.WebviewPanel | undefined;
let previewHtml = "";
let previewUri: string | undefined;
let previewData: unknown;
let previewRefreshTimer: ReturnType<typeof setTimeout> | undefined;
let previewRequest = 0;
let remoteImagesEnabled = false;

function platformDirectory(): string {
  const platform = process.platform === "darwin" ? "darwin" :
    process.platform === "win32" ? "win32" : "linux";
  return `${platform}-${process.arch}`;
}

function serverExecutable(context: vscode.ExtensionContext): string {
  const configured = vscode.workspace.getConfiguration("email-markup").get<string>("server.path", "").trim();
  if (configured) return configured;
  const executable = process.platform === "win32" ? "email-markup-lsp.exe" : "email-markup-lsp";
  return context.asAbsolutePath(path.join("server", platformDirectory(), executable));
}

async function startServer(context: vscode.ExtensionContext): Promise<void> {
  if (!vscode.workspace.isTrusted) {
    output?.warn("Language server disabled because the workspace is not trusted.");
    return;
  }
  const executable = serverExecutable(context);
  output?.info(`Starting language server: ${executable}`);
  const serverOptions: ServerOptions = {
    run: { command: executable, transport: TransportKind.stdio },
    debug: { command: executable, transport: TransportKind.stdio },
  };
  const watcher = vscode.workspace.createFileSystemWatcher("**/{*.em,em.json,*.json}");
  context.subscriptions.push(watcher);
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "email-markup" }],
    outputChannel: output,
    synchronize: { fileEvents: watcher },
  };
  client = new LanguageClient("email-markup", "Email Markup Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);
  await client.start();
  output?.info("Language server started.");
  const version = await client.sendRequest<{ version: number }>("email-markup/protocolVersion");
  if (version.version !== protocolVersion) {
    await client.stop();
    client = undefined;
    throw new Error(`Email Markup client protocol ${protocolVersion} does not match server protocol ${version.version}.`);
  }
  output?.info(`Protocol version ${version.version} confirmed.`);
}

function activeEmailMarkupEditor(): vscode.TextEditor | undefined {
  const editor = vscode.window.activeTextEditor;
  return editor?.document.languageId === "email-markup" ? editor : undefined;
}

function clearPreviewSession(): void {
  if (previewRefreshTimer) clearTimeout(previewRefreshTimer);
  previewRefreshTimer = undefined;
  previewUri = undefined;
  previewData = undefined;
  previewHtml = "";
  ++previewRequest;
}

async function refreshPreview(document: vscode.TextDocument, reveal: boolean): Promise<void> {
  if (!client || previewUri !== document.uri.toString()) return;
  const uri = previewUri;
  const request = ++previewRequest;
  const params: { uri: string; data?: unknown } = { uri };
  if (previewData !== undefined) params.data = previewData;
  let result: { version: number; html: unknown };
  try {
    result = await client.sendRequest<{ version: number; html: unknown }>("email-markup/preview", params);
  } catch (error) {
    if (request === previewRequest && previewUri === uri) {
      output?.error(`Preview refresh failed for ${document.uri.fsPath}.`, error);
      if (reveal) void vscode.window.showErrorMessage("Email Markup preview failed. See Output → Email Markup.");
    }
    return;
  }
  if (request !== previewRequest || previewUri !== uri) return;
  if (result.version !== document.version) {
    output?.warn("Preview response was stale and was ignored.");
    return;
  }
  if (result.html === null) {
    output?.warn("Preview was not generated because the document has compilation errors.");
    return;
  }
  if (typeof result.html !== "string") {
    const actual = Array.isArray(result.html) ? "array" : typeof result.html;
    output?.error(`Language server returned invalid preview HTML (${actual} instead of string).`);
    void vscode.window.showErrorMessage("Email Markup language server returned an invalid preview response. See Output → Email Markup.");
    return;
  }
  previewHtml = result.html;
  remoteImagesEnabled = false;
  if (!previewPanel) {
    previewPanel = vscode.window.createWebviewPanel(
      "emailMarkupPreview",
      "Email Markup Secure Preview",
      vscode.ViewColumn.Beside,
      { enableScripts: false, localResourceRoots: [] },
    );
    previewPanel.onDidDispose(() => {
      previewPanel = undefined;
      clearPreviewSession();
      output?.info("Preview detached from its Email Markup document.");
    });
  }
  previewPanel.title = `Email Markup Preview: ${path.basename(document.uri.fsPath)}`;
  previewPanel.webview.html = previewDocument(previewHtml, false);
  if (reveal) previewPanel.reveal(vscode.ViewColumn.Beside, true);
  output?.debug(`Preview refreshed for ${document.uri.fsPath} at version ${document.version}.`);
}

async function openPreview(withData: boolean): Promise<void> {
  output?.info(`Preview requested${withData ? " with inline JSON data" : ""}.`);
  if (!vscode.workspace.isTrusted) {
    output?.warn("Preview blocked because the workspace is not trusted.");
    void vscode.window.showWarningMessage("Trust this workspace before compiling an Email Markup preview.");
    return;
  }
  if (!client) {
    output?.error("Preview unavailable because the language server is not running.");
    void vscode.window.showErrorMessage("Email Markup language server is not running.");
    return;
  }
  const editor = activeEmailMarkupEditor();
  if (!editor) {
    output?.warn("Preview ignored because the active editor is not an Email Markup document.");
    return;
  }
  let data: unknown;
  if (withData) {
    const raw = await vscode.window.showInputBox({
      title: "Unsaved preview JSON",
      prompt: "Enter one JSON object. It is sent to email-markup-lsp for this preview only.",
      value: "{}",
      ignoreFocusOut: true,
    });
    if (raw === undefined) return;
    try { data = JSON.parse(raw); }
    catch {
      output?.warn("Preview data is not valid JSON.");
      void vscode.window.showErrorMessage("Preview data must be valid JSON.");
      return;
    }
    if (typeof data !== "object" || data === null || Array.isArray(data)) {
      void vscode.window.showErrorMessage("Preview data must be a JSON object.");
      return;
    }
  }
  previewUri = editor.document.uri.toString();
  previewData = data;
  output?.info(`Preview attached to ${editor.document.uri.fsPath}.`);
  await refreshPreview(editor.document, true);
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  output = vscode.window.createOutputChannel("Email Markup", { log: true });
  context.subscriptions.push(output);
  output.info(`Activating Email Markup Language Support ${context.extension.packageJSON.version ?? "unknown"}.`);
  registerWebFeatures(context);
  output.info("Registered Email Markup commands and embedded HTML/CSS editor features.");
  context.subscriptions.push(
    vscode.commands.registerCommand("email-markup.preview", () => openPreview(false)),
    vscode.commands.registerCommand("email-markup.previewWithData", () => openPreview(true)),
    vscode.commands.registerCommand("email-markup.loadRemoteImages", () => {
      if (!previewPanel || !previewHtml || remoteImagesEnabled) return;
      remoteImagesEnabled = true;
      previewPanel.webview.html = previewDocument(previewHtml, true);
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (!previewPanel || event.document.uri.toString() !== previewUri) return;
      if (previewRefreshTimer) clearTimeout(previewRefreshTimer);
      previewRefreshTimer = setTimeout(() => {
        previewRefreshTimer = undefined;
        void refreshPreview(event.document, false);
      }, previewRefreshDelayMs);
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (document.uri.toString() !== previewUri) return;
      if (previewPanel) previewPanel.dispose();
      else clearPreviewSession();
    }),
  );
  if (vscode.workspace.isTrusted) {
    try { await startServer(context); }
    catch (error) {
      output.error("Language server failed to start.", error);
      void vscode.window.showErrorMessage(`Email Markup language server failed to start: ${String(error)}`);
    }
  } else {
    output.warn("Workspace is untrusted; only syntax highlighting is enabled.");
  }
}

export async function deactivate(): Promise<void> {
  if (client) {
    output?.info("Stopping language server.");
    await client.stop();
  }
}
