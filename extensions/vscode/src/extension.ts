import * as path from "node:path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

import { previewDocument } from "./preview";

const protocolVersion = 1;

let client: LanguageClient | undefined;
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
  if (!vscode.workspace.isTrusted) return;
  const executable = serverExecutable(context);
  const serverOptions: ServerOptions = {
    run: { command: executable, transport: TransportKind.stdio },
    debug: { command: executable, transport: TransportKind.stdio },
  };
  const watcher = vscode.workspace.createFileSystemWatcher("**/{*.ell,ell.json,*.json}");
  context.subscriptions.push(watcher);
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "ell" }],
    synchronize: { fileEvents: watcher },
  };
  client = new LanguageClient("ell", "ELL Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client);
  await client.start();
  const version = await client.sendRequest<{ version: number }>("ell/protocolVersion");
  if (version.version !== protocolVersion) {
    await client.stop();
    client = undefined;
    throw new Error(`ELL client protocol ${protocolVersion} does not match server protocol ${version.version}.`);
  }
}

function activeEllEditor(): vscode.TextEditor | undefined {
  const editor = vscode.window.activeTextEditor;
  return editor?.document.languageId === "ell" ? editor : undefined;
}

async function openPreview(withData: boolean): Promise<void> {
  if (!vscode.workspace.isTrusted) {
    void vscode.window.showWarningMessage("Trust this workspace before compiling an ELL preview.");
    return;
  }
  if (!client) {
    void vscode.window.showErrorMessage("ELL language server is not running.");
    return;
  }
  const editor = activeEllEditor();
  if (!editor) return;
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
    catch { void vscode.window.showErrorMessage("Preview data must be valid JSON."); return; }
    if (typeof data !== "object" || data === null || Array.isArray(data)) {
      void vscode.window.showErrorMessage("Preview data must be a JSON object.");
      return;
    }
  }
  const params: { uri: string; data?: unknown } = { uri: editor.document.uri.toString() };
  if (withData) params.data = data;
  const result = await client.sendRequest<{ version: number; html: string | null }>("ell/preview", params);
  if (result.version !== editor.document.version || !result.html) return;
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
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
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
      void vscode.window.showErrorMessage(`ELL language server failed to start: ${String(error)}`);
    }
  }
}

export async function deactivate(): Promise<void> {
  if (client) await client.stop();
}
