import * as vscode from "vscode";
import { getCSSLanguageService } from "vscode-css-languageservice";
import { getLanguageService as getHTMLLanguageService } from "vscode-html-languageservice";
import { TextDocument } from "vscode-languageserver-textdocument";

import { cssProjection, inRanges, localClasses } from "./webProjection";

const selector: vscode.DocumentSelector = [{ language: "ell", scheme: "file" }];
const html = getHTMLLanguageService();
const css = getCSSLanguageService();

function languageDocument(document: vscode.TextDocument, language: string, text = document.getText()): TextDocument {
  return TextDocument.create(document.uri.toString(), language, document.version, text);
}

export function registerWebFeatures(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(selector, {
      provideCompletionItems(document, position) {
        const source = document.getText();
        const projection = cssProjection(source);
        const offset = document.offsetAt(position);
        if (inRanges(offset, projection.ranges)) {
          const cssDocument = languageDocument(document, "css", projection.text);
          return css.doComplete(cssDocument, position, css.parseStylesheet(cssDocument)) as unknown as vscode.CompletionList;
        }

        const linePrefix = document.lineAt(position).text.slice(0, position.character);
        if (/\bclass\s*=\s*["'][^"']*$/.test(linePrefix)) {
          return localClasses(source).map((name) => {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Value);
            item.detail = "CSS class declared in this ELL document";
            return item;
          });
        }

        const htmlDocument = languageDocument(document, "html");
        return html.doComplete(htmlDocument, position, html.parseHTMLDocument(htmlDocument)) as unknown as vscode.CompletionList;
      },
    }, "<", " ", ":", ".", "\"", "'"),
    vscode.languages.registerHoverProvider(selector, {
      provideHover(document, position) {
        const source = document.getText();
        const projection = cssProjection(source);
        if (inRanges(document.offsetAt(position), projection.ranges)) {
          const cssDocument = languageDocument(document, "css", projection.text);
          return css.doHover(cssDocument, position, css.parseStylesheet(cssDocument)) as unknown as vscode.Hover;
        }
        const htmlDocument = languageDocument(document, "html");
        return html.doHover(htmlDocument, position, html.parseHTMLDocument(htmlDocument)) as unknown as vscode.Hover;
      },
    }),
    vscode.languages.registerDocumentSymbolProvider(selector, {
      provideDocumentSymbols(document) {
        const htmlDocument = languageDocument(document, "html");
        return html.findDocumentSymbols2(htmlDocument, html.parseHTMLDocument(htmlDocument)) as unknown as vscode.DocumentSymbol[];
      },
    }),
    vscode.languages.registerLinkedEditingRangeProvider(selector, {
      provideLinkedEditingRanges(document, position) {
        const htmlDocument = languageDocument(document, "html");
        const ranges = html.findLinkedEditingRanges(
          htmlDocument,
          position,
          html.parseHTMLDocument(htmlDocument),
        );
        return ranges ? {
          ranges: ranges as vscode.Range[],
          wordPattern: /[-_a-zA-Z0-9]+/,
        } : undefined;
      },
    }),
    vscode.languages.registerColorProvider(selector, {
      provideDocumentColors(document) {
        const projection = cssProjection(document.getText());
        const cssDocument = languageDocument(document, "css", projection.text);
        return css.findDocumentColors(
          cssDocument,
          css.parseStylesheet(cssDocument),
        ) as unknown as vscode.ColorInformation[];
      },
      provideColorPresentations(color, context) {
        const projection = cssProjection(context.document.getText());
        const cssDocument = languageDocument(context.document, "css", projection.text);
        return css.getColorPresentations(
          cssDocument,
          css.parseStylesheet(cssDocument),
          color,
          context.range,
        ) as unknown as vscode.ColorPresentation[];
      },
    }),
  );
}
