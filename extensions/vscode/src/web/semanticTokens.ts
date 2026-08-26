import * as vscode from "vscode";

import { maskNonCode } from "./definitions";

/**
 * Semantic highlighting for Email Markup.
 *
 * This deliberately does not re-lex the language — the TextMate grammar already
 * colours strings, numbers, comments, punctuation and the embedded HTML and CSS,
 * and duplicating that here would only create two things to disagree with each
 * other. What a grammar cannot do is tell a defined component from a typo, or a
 * project token from an ordinary interpolation, because that needs the whole
 * project rather than one line. Those are exactly the distinctions produced here,
 * layered on top of the grammar the way semantic tokens are meant to be.
 */

export const semanticTokenLegend = new vscode.SemanticTokensLegend(
  ["keyword", "class", "property", "variable", "parameter", "type"],
  ["declaration", "defaultLibrary", "modification"],
);

export interface ProjectVocabulary {
  keywords: ReadonlySet<string>;
  propTypes: ReadonlySet<string>;
  components: ReadonlySet<string>;
  tokens: ReadonlySet<string>;
}

function pushToken(
  builder: vscode.SemanticTokensBuilder,
  document: vscode.TextDocument,
  offset: number,
  length: number,
  type: string,
  modifiers: string[] = [],
): void {
  if (length <= 0) return;
  const start = document.positionAt(offset);
  const end = document.positionAt(offset + length);
  // A semantic token may not span lines; anything that would is left to the grammar.
  if (start.line !== end.line) return;
  builder.push(start.line, start.character, length, ...encode(type, modifiers));
}

function encode(type: string, modifiers: string[]): [number, number] {
  const typeIndex = semanticTokenLegend.tokenTypes.indexOf(type);
  let mask = 0;
  for (const modifier of modifiers) {
    const index = semanticTokenLegend.tokenModifiers.indexOf(modifier);
    if (index >= 0) mask |= 1 << index;
  }
  return [typeIndex, mask];
}

/** Offsets of every `@Props`/`@Params` body, where declarations rather than uses live. */
function declarationBlocks(masked: string): Array<[number, number]> {
  const blocks: Array<[number, number]> = [];
  for (const keyword of ["Props", "Params"]) {
    let cursor = 0;
    for (;;) {
      const open = masked.indexOf(`@${keyword}`, cursor);
      if (open === -1) break;
      const close = masked.indexOf(`@/${keyword}`, open);
      if (close === -1) break;
      blocks.push([open + keyword.length + 1, close]);
      cursor = close + keyword.length + 2;
    }
  }
  return blocks;
}

export function buildSemanticTokens(
  document: vscode.TextDocument,
  vocabulary: ProjectVocabulary,
): vscode.SemanticTokens {
  const source = document.getText();
  const masked = maskNonCode(source);
  const builder = new vscode.SemanticTokensBuilder(semanticTokenLegend);

  // `@DefineComponent(name: "X")` — the name is the declaration of a component.
  for (const match of masked.matchAll(
    /@DefineComponent\s*\(\s*name\s*:\s*"([A-Za-z_][A-Za-z0-9_]*)"/gu,
  )) {
    const at = (match.index ?? 0) + match[0].lastIndexOf(match[1]);
    pushToken(builder, document, at, match[1].length, "class", ["declaration"]);
  }

  // `@DefineToken(name: "x")` — likewise for tokens.
  for (const match of masked.matchAll(
    /@DefineToken\s*\(\s*name\s*:\s*"([A-Za-z_][A-Za-z0-9_]*)"/gu,
  )) {
    const at = (match.index ?? 0) + match[0].lastIndexOf(match[1]);
    pushToken(builder, document, at, match[1].length, "property", ["declaration"]);
  }

  // Every `@Name` sigil: a language keyword, a component the project defines, or
  // something the author expects to be one.
  for (const match of masked.matchAll(/@\/?([A-Z][A-Za-z0-9_]*)/gu)) {
    const name = match[1];
    const at = (match.index ?? 0) + match[0].indexOf(name);
    if (vocabulary.keywords.has(name)) {
      pushToken(builder, document, at, name.length, "keyword");
    } else if (vocabulary.components.has(name)) {
      pushToken(builder, document, at, name.length, "class");
    }
  }

  // `@{token.name}` resolves against the project's tokens; `@{name}` is a local.
  for (const match of masked.matchAll(/@\{\s*([A-Za-z_][A-Za-z0-9_.]*)\s*\}/gu)) {
    const expression = match[1];
    const at = (match.index ?? 0) + match[0].indexOf(expression);
    if (expression.startsWith("token.")) {
      const name = expression.slice("token.".length);
      pushToken(builder, document, at, "token".length, "keyword", ["defaultLibrary"]);
      pushToken(
        builder,
        document,
        at + "token.".length,
        name.length,
        "property",
        vocabulary.tokens.has(name) ? ["defaultLibrary"] : [],
      );
    } else {
      pushToken(builder, document, at, expression.split(".")[0].length, "variable");
    }
  }

  // `@[context.path]` is a deferred value resolved from the message context.
  for (const match of masked.matchAll(/@\[\s*([A-Za-z_][A-Za-z0-9_.]*)\s*\]/gu)) {
    const expression = match[1];
    const at = (match.index ?? 0) + match[0].indexOf(expression);
    pushToken(builder, document, at, expression.length, "variable", ["modification"]);
  }

  // Declarations inside `@Props`/`@Params`: the name is a parameter, and what
  // follows the colon is one of the language's own types.
  for (const [open, close] of declarationBlocks(masked)) {
    const body = source.slice(open, close);
    for (const match of body.matchAll(
      /^([ \t]*)([A-Za-z_][A-Za-z0-9_]*)(\s*:\s*)([A-Za-z]+)/gmu,
    )) {
      const lineStart = open + (match.index ?? 0);
      const nameAt = lineStart + match[1].length;
      pushToken(builder, document, nameAt, match[2].length, "parameter", ["declaration"]);
      if (vocabulary.propTypes.has(match[4])) {
        const typeAt = nameAt + match[2].length + match[3].length;
        pushToken(builder, document, typeAt, match[4].length, "type");
      }
    }
  }

  return builder.build();
}

export class EmailMarkupSemanticTokensProvider
  implements vscode.DocumentSemanticTokensProvider
{
  constructor(private readonly vocabulary: () => ProjectVocabulary) {}

  provideDocumentSemanticTokens(document: vscode.TextDocument): vscode.SemanticTokens {
    return buildSemanticTokens(document, this.vocabulary());
  }
}
