import assert from "node:assert/strict";
import * as vscode from "vscode";

function label(item: vscode.CompletionItem): string {
  return typeof item.label === "string" ? item.label : item.label.label;
}

async function completions(
  document: vscode.TextDocument,
  offset: number,
): Promise<vscode.CompletionList> {
  return vscode.commands.executeCommand<vscode.CompletionList>(
    "vscode.executeCompletionItemProvider",
    document.uri,
    document.positionAt(offset),
  );
}

async function eventually(check: () => Promise<boolean>, message: string): Promise<void> {
  for (let attempt = 0; attempt < 40; ++attempt) {
    if (await check()) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  assert.fail(message);
}

export async function run(): Promise<void> {
  const extension = vscode.extensions.getExtension("ell-lang.ell-language");
  assert.ok(extension, "ELL extension is installed in the development host");
  await extension.activate();
  assert.equal(extension.isActive, true);

  const workspace = vscode.workspace.workspaceFolders?.[0];
  assert.ok(workspace, "CSS-inlining example workspace opened");
  const uri = vscode.Uri.joinPath(workspace.uri, "message.ell");
  const document = await vscode.workspace.openTextDocument(uri);
  const editor = await vscode.window.showTextDocument(document);
  const source = document.getText();

  await eventually(async () => {
    const list = await completions(document, 0);
    return list.items.some((item) => label(item) === "@Paragraph");
  }, "bundled ell-lsp did not provide component completion");

  const htmlOffset = source.indexOf("<section") + 1;
  const htmlItems = await completions(document, htmlOffset);
  assert.ok(htmlItems.items.some((item) => label(item) === "div"), "HTML completion is passed through");

  const styleMarker = "style: \"release-card";
  const styleOffset = source.indexOf(styleMarker) + "style: \"".length;
  const styleItems = await completions(document, styleOffset);
  assert.ok(styleItems.items.some((item) => label(item) === "release-card"), "ELL style bundles complete in style arguments");

  const cssOffset = source.indexOf("background: #") + "background: ".length;
  const cssItems = await completions(document, cssOffset);
  assert.ok(cssItems.items.length > 5, "embedded CSS value completion is available");

  await editor.edit((edit) => edit.insert(document.positionAt(document.getText().length), "\n@// Zażółć 😀"));
  await eventually(async () => {
    const list = await completions(document, 0);
    return list.items.some((item) => label(item) === "@Heading");
  }, "ell-lsp stopped responding after a Unicode edit");
  await vscode.commands.executeCommand("undo");

  await vscode.commands.executeCommand("ell.preview");
  const commands = await vscode.commands.getCommands();
  assert.ok(commands.includes("ell.loadRemoteImages"));

  const manifest = extension.packageJSON as {
    capabilities?: { untrustedWorkspaces?: { supported?: string } };
  };
  assert.equal(manifest.capabilities?.untrustedWorkspaces?.supported, "limited");
}
