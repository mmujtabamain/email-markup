export type OffsetRange = readonly [start: number, end: number];

export interface CSSProjection {
  text: string;
  ranges: OffsetRange[];
}

function blank(source: string): string[] {
  return source.split("").map((character) => character === "\n" ? "\n" : " ");
}

function copy(characters: string[], source: string, start: number, end: number): void {
  for (let index = start; index < end; ++index) characters[index] = source[index];
}

function commentMask(source: string): boolean[] {
  const active = source.split("").map(() => true);
  for (let index = 0; index < source.length;) {
    if (source.startsWith("@*", index)) {
      const close = source.indexOf("*@", index + 2);
      const end = close === -1 ? source.length : close + 2;
      active.fill(false, index, end);
      index = end;
    } else if (source.startsWith("@//", index)) {
      const close = source.indexOf("\n", index + 3);
      const end = close === -1 ? source.length : close;
      active.fill(false, index, end);
      index = end;
    } else {
      ++index;
    }
  }
  return active;
}

function directiveHeadEnd(source: string, start: number, name: string): number {
  let index = start + name.length + 1;
  while (index < source.length && /\s/.test(source[index])) ++index;
  if (source[index] !== "(") return index;
  let depth = 0;
  let quote = "";
  for (; index < source.length; ++index) {
    const character = source[index];
    if (quote) {
      if (character === "\\") ++index;
      else if (character === quote) quote = "";
    } else if (character === "\"" || character === "'") {
      quote = character;
    } else if (character === "(") {
      ++depth;
    } else if (character === ")" && --depth === 0) {
      return index + 1;
    }
  }
  return source.length;
}

function blockRanges(source: string, name: string): OffsetRange[] {
  const ranges: OffsetRange[] = [];
  const active = commentMask(source);
  const pattern = new RegExp(`@${name}\\b`, "g");
  for (const match of source.matchAll(pattern)) {
    const open = match.index ?? 0;
    if (!active[open]) continue;
    const start = directiveHeadEnd(source, open, name);
    const close = new RegExp(`@/${name}\\b`, "g");
    close.lastIndex = start;
    const ending = close.exec(source);
    if (ending) ranges.push([start, ending.index]);
  }
  return ranges;
}

function stylesheetRanges(source: string): OffsetRange[] {
  const ranges: OffsetRange[] = [];
  const projected = htmlProjection(source);
  const stylePattern = /<style(?:\s[^>]*)?>([\s\S]*?)<\/style\s*>/gi;
  for (const match of projected.matchAll(stylePattern)) {
    const body = match[1];
    const start = (match.index ?? 0) + match[0].indexOf(body);
    ranges.push([start, start + body.length]);
  }
  ranges.push(...blockRanges(source, "Media"));
  return ranges;
}

function declarationRanges(source: string): OffsetRange[] {
  const ranges = blockRanges(source, "DefineStyle");
  const projected = htmlProjection(source);
  const inlinePattern = /\bstyle\s*=\s*(["'])([\s\S]*?)\1/gi;
  for (const match of projected.matchAll(inlinePattern)) {
    const body = match[2];
    const start = (match.index ?? 0) + match[0].indexOf(body);
    ranges.push([start, start + body.length]);
  }
  return ranges;
}

function wrapperPosition(source: string, start: number): number | undefined {
  const lineStart = source.lastIndexOf("\n", Math.max(0, start - 1)) + 1;
  if (start - lineStart >= 3) return lineStart;
  return undefined;
}

export function cssProjection(source: string): CSSProjection {
  const characters = blank(source);
  const stylesheets = stylesheetRanges(source);
  const declarations = declarationRanges(source);
  for (const [start, end] of [...stylesheets, ...declarations]) copy(characters, source, start, end);

  for (const [start, end] of [...stylesheets, ...declarations]) {
    for (let index = start; index < end;) {
      if (!source.startsWith("@{", index)) {
        ++index;
        continue;
      }
      const close = source.indexOf("}", index + 2);
      if (close === -1 || close >= end) break;
      characters[index] = "0";
      for (let cursor = index + 1; cursor <= close; ++cursor) {
        if (characters[cursor] !== "\n") characters[cursor] = " ";
      }
      index = close + 1;
    }
  }

  for (const [start, end] of declarations) {
    const open = wrapperPosition(source, start);
    if (open !== undefined) {
      characters[open] = ".";
      characters[open + 1] = "x";
      characters[open + 2] = "{";
    }
    if (end < characters.length && characters[end] !== "\n") characters[end] = "}";
  }
  return { text: characters.join(""), ranges: [...stylesheets, ...declarations] };
}

function directiveEnd(source: string, start: number): number {
  let index = start;
  let depth = 0;
  let quote = "";
  while (index < source.length) {
    const character = source[index];
    if (quote) {
      if (character === "\\") ++index;
      else if (character === quote) quote = "";
    } else if (character === "\"" || character === "'") {
      quote = character;
    } else if (character === "(") {
      ++depth;
    } else if (character === ")") {
      if (depth === 0) return index;
      --depth;
    } else if (depth === 0 && (character === ";" || character === "\n" || character === "<")) {
      return index;
    } else if (depth === 0 && /\s/.test(character)) {
      return index;
    }
    ++index;
  }
  return index;
}

function maskRange(characters: string[], start: number, end: number): void {
  for (let index = start; index < end; ++index) {
    if (characters[index] !== "\n") characters[index] = " ";
  }
}

export function htmlProjection(source: string): string {
  const characters = source.split("");
  for (const name of ["DefineStyle", "Media", "Props", "Slots"]) {
    const openPattern = new RegExp(`@${name}\\b`, "g");
    for (const match of source.matchAll(openPattern)) {
      const closePattern = new RegExp(`@/${name}\\b`, "g");
      closePattern.lastIndex = (match.index ?? 0) + match[0].length;
      const close = closePattern.exec(source);
      if (close) maskRange(characters, match.index ?? 0, close.index + close[0].length);
    }
  }

  for (let index = 0; index < source.length;) {
    if (source.startsWith("@*", index)) {
      const end = source.indexOf("*@", index + 2);
      const limit = end === -1 ? source.length : end + 2;
      maskRange(characters, index, limit);
      index = limit;
    } else if (source.startsWith("@//", index)) {
      const end = source.indexOf("\n", index + 3);
      const limit = end === -1 ? source.length : end;
      maskRange(characters, index, limit);
      index = limit;
    } else if (source.startsWith("@{", index)) {
      const end = source.indexOf("}", index + 2);
      const limit = end === -1 ? source.length : end + 1;
      maskRange(characters, index, limit);
      if (limit > index) characters[index] = "x";
      index = limit;
    } else {
      const directive = source.slice(index).match(/^@\/?[A-Z][A-Za-z0-9_]*/);
      if (!directive) {
        ++index;
        continue;
      }
      const limit = directiveEnd(source, index + directive[0].length);
      maskRange(characters, index, limit);
      index = limit;
    }
  }
  return characters.join("");
}

export function inRanges(offset: number, ranges: readonly OffsetRange[]): boolean {
  return ranges.some(([start, end]) => offset >= start && offset <= end);
}

export function localClasses(source: string): string[] {
  const names = new Set<string>();
  const projection = cssProjection(source).text;
  for (const match of projection.matchAll(/\.([_a-zA-Z][_a-zA-Z0-9-]*)/g)) {
    if (match[1] !== "x") names.add(match[1]);
  }
  return [...names].sort();
}

export function isClassAttributeContext(source: string, offset: number): boolean {
  const tagStart = source.lastIndexOf("<", Math.max(0, offset - 1));
  const tagEnd = source.lastIndexOf(">", Math.max(0, offset - 1));
  if (tagStart <= tagEnd) return false;
  return /\bclass\s*=\s*["'][^"']*$/i.test(source.slice(tagStart, offset));
}

function isHtmlTagContext(source: string, offset: number): boolean {
  const before = source.slice(0, offset);
  const tagStart = before.lastIndexOf("<");
  return tagStart > before.lastIndexOf(">") && !/^<style\b[^>]*>[\s\S]*$/i.test(before.slice(tagStart));
}

export function isEmmetContext(source: string, offset: number): boolean {
  const lineStart = source.lastIndexOf("\n", Math.max(0, offset - 1)) + 1;
  const abbreviation = source.slice(lineStart, offset).trim();
  if (!abbreviation || /\s/.test(abbreviation)) return false;
  return /^(?:[A-Za-z][\w-]*)?(?:[.#][\w-]*)+(?:[>+][A-Za-z.#][\w.#-]*)*$/.test(abbreviation)
    || /^[A-Za-z][\w-]*(?:[>+][A-Za-z.#][\w.#-]*)+$/.test(abbreviation);
}

export function webCompletionLanguage(source: string, offset: number): "html" | "css" | undefined {
  if (inRanges(offset, cssProjection(source).ranges)) return "css";
  if (isHtmlTagContext(source, offset) || isEmmetContext(source, offset)) return "html";
  return undefined;
}

export function htmlLinkedRanges(source: string, offset: number): OffsetRange[] {
  const projected = htmlProjection(source);
  const tags: Array<{ name: string; range: OffsetRange; closing: boolean; selfClosing: boolean }> = [];
  for (const match of projected.matchAll(/<\s*(\/?)\s*([A-Za-z][\w:-]*)\b[^>]*>/g)) {
    const wholeStart = match.index ?? 0;
    const nameStart = wholeStart + match[0].indexOf(match[2]);
    tags.push({
      name: match[2].toLowerCase(),
      range: [nameStart, nameStart + match[2].length],
      closing: match[1] === "/",
      selfClosing: /\/\s*>$/.test(match[0]),
    });
  }
  const selected = tags.findIndex(({ range: [start, end] }) => offset >= start && offset <= end);
  if (selected === -1) return [];
  const target = tags[selected];
  let depth = 0;
  const direction = target.closing ? -1 : 1;
  for (let index = selected + direction; index >= 0 && index < tags.length; index += direction) {
    const candidate = tags[index];
    if (candidate.name !== target.name || candidate.selfClosing) continue;
    if (candidate.closing === target.closing) ++depth;
    else if (depth > 0) --depth;
    else return [target.range, candidate.range];
  }
  return [];
}
