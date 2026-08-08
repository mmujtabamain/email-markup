import * as vscode from "vscode";

import {
  cssProjection,
  htmlLinkedRanges,
  htmlProjection,
  isClassAttributeContext,
  localClasses,
  webCompletionLanguage,
} from "./webProjection";

const selector: vscode.DocumentSelector = [
  { language: "email-markup", scheme: "file" },
];
const embeddedScheme = "email-markup-embedded";

class EmbeddedDocuments implements vscode.TextDocumentContentProvider {
  private readonly contents = new Map<string, string>();
  private readonly latest = new Map<string, string>();
  private readonly activated = new Set<string>();

  provideTextDocumentContent(uri: vscode.Uri): string | undefined {
    return this.contents.get(uri.toString(true));
  }

  uri(document: vscode.TextDocument, language: "html" | "css"): vscode.Uri {
    const original = encodeURIComponent(document.uri.toString(true));
    const uri = vscode.Uri.parse(
      `${embeddedScheme}://${language}/${original}.${language}?version=${document.version}`,
    );
    const identity = `${document.uri.toString(true)}\0${language}`;
    const previous = this.latest.get(identity);
    if (previous) this.contents.delete(previous);
    const source = document.getText();
    const key = uri.toString(true);
    this.latest.set(identity, key);
    this.contents.set(
      key,
      language === "html" ? htmlProjection(source) : cssProjection(source).text,
    );
    return uri;
  }

  async open(
    document: vscode.TextDocument,
    language: "html" | "css",
  ): Promise<vscode.TextDocument> {
    const virtual = await vscode.workspace.openTextDocument(
      this.uri(document, language),
    );
    if (!this.activated.has(language)) {
      const builtin = vscode.extensions.getExtension(
        `vscode.${language}-language-features`,
      );
      if (builtin) await builtin.activate();
      this.activated.add(language);
    }
    return virtual;
  }
}

function itemLabel(item: vscode.CompletionItem): string {
  return typeof item.label === "string" ? item.label : item.label.label;
}

async function forwardedCompletions(
  documents: EmbeddedDocuments,
  document: vscode.TextDocument,
  position: vscode.Position,
  language: "html" | "css",
  token: vscode.CancellationToken,
): Promise<vscode.CompletionList> {
  const virtual = await documents.open(document, language);
  const virtualPosition = virtual.positionAt(document.offsetAt(position));
  const result = await vscode.commands.executeCommand<vscode.CompletionList>(
    "vscode.executeCompletionItemProvider",
    virtual.uri,
    virtualPosition,
  );
  return token.isCancellationRequested
    ? new vscode.CompletionList()
    : (result ?? new vscode.CompletionList());
}

export function registerWebFeatures(context: vscode.ExtensionContext): void {
  const documents = new EmbeddedDocuments();
  context.subscriptions.push(
    vscode.workspace.registerTextDocumentContentProvider(
      embeddedScheme,
      documents,
    ),
    vscode.languages.registerCompletionItemProvider(
      selector,
      {
        async provideCompletionItems(
          document,
          position,
          token,
          _completionContext,
        ) {
          const source = document.getText();
          const language = webCompletionLanguage(
            source,
            document.offsetAt(position),
          );
          if (!language) return new vscode.CompletionList();
          const forwarded = await forwardedCompletions(
            documents,
            document,
            position,
            language,
            token,
          );
          if (
            language !== "html" ||
            !isClassAttributeContext(source, document.offsetAt(position))
          ) {
            return forwarded;
          }

          const labels = new Set(forwarded.items.map(itemLabel));
          for (const name of localClasses(source)) {
            if (labels.has(name)) continue;
            const item = new vscode.CompletionItem(
              name,
              vscode.CompletionItemKind.Value,
            );
            item.detail = "CSS class declared in this Email Markup document";
            forwarded.items.push(item);
          }
          return forwarded;
        },
      },
      "<",
      " ",
      ":",
      ".",
      '"',
      "'",
      "/",
      "-",
      ">",
    ),
    vscode.languages.registerHoverProvider(selector, {
      async provideHover(document, position, token) {
        const source = document.getText();
        const language =
          webCompletionLanguage(source, document.offsetAt(position)) ??
          (source[document.offsetAt(position)] === "<" ? "html" : undefined);
        if (!language) return undefined;
        const virtual = await documents.open(document, language);
        const hovers = await vscode.commands.executeCommand<vscode.Hover[]>(
          "vscode.executeHoverProvider",
          virtual.uri,
          virtual.positionAt(document.offsetAt(position)),
        );
        return token.isCancellationRequested || !hovers?.length
          ? undefined
          : hovers[0];
      },
    }),
    vscode.languages.registerDocumentSymbolProvider(selector, {
      async provideDocumentSymbols(document, token) {
        const virtual = await documents.open(document, "html");
        const symbols = await vscode.commands.executeCommand<
          vscode.DocumentSymbol[]
        >("vscode.executeDocumentSymbolProvider", virtual.uri);
        return token.isCancellationRequested ? [] : (symbols ?? []);
      },
    }),
    vscode.languages.registerLinkedEditingRangeProvider(selector, {
      provideLinkedEditingRanges(document, position) {
        const ranges = htmlLinkedRanges(
          document.getText(),
          document.offsetAt(position),
        );
        return ranges.length === 2
          ? {
              ranges: ranges.map(
                ([start, end]) =>
                  new vscode.Range(
                    document.positionAt(start),
                    document.positionAt(end),
                  ),
              ),
              wordPattern: /[-_a-zA-Z0-9:]+/,
            }
          : undefined;
      },
    }),
    vscode.languages.registerColorProvider(selector, {
      async provideDocumentColors(document, token) {
        const virtual = await documents.open(document, "css");
        const colors = await vscode.commands.executeCommand<
          vscode.ColorInformation[]
        >("vscode.executeDocumentColorProvider", virtual.uri);
        return token.isCancellationRequested ? [] : (colors ?? []);
      },
      async provideColorPresentations(color, colorContext, token) {
        const virtual = await documents.open(colorContext.document, "css");
        const presentations = await vscode.commands.executeCommand<
          vscode.ColorPresentation[]
        >("vscode.executeColorPresentationProvider", color, {
          uri: virtual.uri,
          range: colorContext.range,
        });
        return token.isCancellationRequested ? [] : (presentations ?? []);
      },
    }),
  );
}
