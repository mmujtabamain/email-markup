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

let client: LanguageClient | undefined;
let output: vscode.LogOutputChannel | undefined;
let previewPanel: vscode.WebviewPanel | undefined;
let previewHtml = "";
let remoteImagesEnabled = false;

function platformDirectory(): string {
  const platform = process.platform === "darwin" ? "darwin" :
    process.platform === "win32" ? "win32" : "linux";
  return `${platform}-${process.arch}`;
}

function serverExecutable(context: vscode.ExtensionContext): string {
  const configured = vscode.workspace.getConfiguration("ell").get<string>("server.path", "").trim();
  if (configured) return configured;
  const executable = process.platform === "win32" ? "ell-lsp.exe" : "ell-lsp";
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
  const watcher = vscode.workspace.createFileSystemWatcher("**/{*.ell,ell.json,*.json}");
  context.subscriptions.push(watcher);
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "ell" }],
    outputChannel: output,
    synchronize: { fileEvents: watcher },
  };
  client = new LanguageClient("ell", "ELL Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);
  await client.start();
  output?.info("Language server started.");
  const version = await client.sendRequest<{ version: number }>("ell/protocolVersion");
  if (version.version !== protocolVersion) {
    await client.stop();
    client = undefined;
    throw new Error(`ELL client protocol ${protocolVersion} does not match server protocol ${version.version}.`);
  }
  output?.info(`Protocol version ${version.version} confirmed.`);
}

function activeEllEditor(): vscode.TextEditor | undefined {
  const editor = vscode.window.activeTextEditor;
  return editor?.document.languageId === "ell" ? editor : undefined;
}

async function openPreview(withData: boolean): Promise<void> {
  output?.info(`Preview requested${withData ? " with inline JSON data" : ""}.`);
  if (!vscode.workspace.isTrusted) {
    output?.warn("Preview blocked because the workspace is not trusted.");
    void vscode.window.showWarningMessage("Trust this workspace before compiling an ELL preview.");
    return;
  }
  if (!client) {
    output?.error("Preview unavailable because the language server is not running.");
    void vscode.window.showErrorMessage("ELL language server is not running.");
    return;
  }
  const editor = activeEllEditor();
  if (!editor) {
    output?.warn("Preview ignored because the active editor is not an ELL document.");
    return;
  }
  let data: unknown;
  if (withData) {
    const raw = await vscode.window.showInputBox({
      title: "Unsaved preview JSON",
      prompt: "Enter one JSON object. It is sent to ell-lsp for this preview only.",
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
  const params: { uri: string; data?: unknown } = { uri: editor.document.uri.toString() };
  if (withData) params.data = data;
  const result = await client.sendRequest<{ version: number; html: unknown }>("ell/preview", params);
  if (result.version !== editor.document.version) {
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
    void vscode.window.showErrorMessage("ELL language server returned an invalid preview response. See Output → ELL.");
    return;
  }
  previewHtml = result.html;
  remoteImagesEnabled = false;
  if (!previewPanel) {
    previewPanel = vscode.window.createWebviewPanel(
      "ellPreview",
      "ELL Secure Preview",
      vscode.ViewColumn.Beside,
      { enableScripts: false, localResourceRoots: [] },
    );
    previewPanel.onDidDispose(() => { previewPanel = undefined; previewHtml = ""; });
  }
  previewPanel.webview.html = previewDocument(previewHtml, false);
  previewPanel.reveal(vscode.ViewColumn.Beside, true);
  output?.info(`Preview opened for ${editor.document.uri.fsPath}.`);
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  output = vscode.window.createOutputChannel("ELL", { log: true });
  context.subscriptions.push(output);
  output.info(`Activating ELL Language Support ${context.extension.packageJSON.version ?? "unknown"}.`);
  registerWebFeatures(context);
  output.info("Registered ELL commands and embedded HTML/CSS editor features.");
  context.subscriptions.push(
    vscode.commands.registerCommand("ell.preview", () => openPreview(false)),
    vscode.commands.registerCommand("ell.previewWithData", () => openPreview(true)),
    vscode.commands.registerCommand("ell.loadRemoteImages", () => {
      if (!previewPanel || !previewHtml || remoteImagesEnabled) return;
      remoteImagesEnabled = true;
      previewPanel.webview.html = previewDocument(previewHtml, true);
    }),
  );
  if (vscode.workspace.isTrusted) {
    try { await startServer(context); }
    catch (error) {
      output.error("Language server failed to start.", error);
      void vscode.window.showErrorMessage(`ELL language server failed to start: ${String(error)}`);
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
