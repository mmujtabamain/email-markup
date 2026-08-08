export interface LiteralImageReference {
  url: string;
  start: number;
  end: number;
}

function directiveEnd(source: string, start: number): number {
  let depth = 0;
  let quote = "";
  for (let index = start; index < source.length; ++index) {
    const character = source[index];
    if (quote) {
      if (character === "\\") ++index;
      else if (character === quote) quote = "";
    } else if (character === '"' || character === "'") {
      quote = character;
    } else if (character === "(") {
      ++depth;
    } else if (character === ")" && --depth === 0) {
      return index + 1;
    }
  }
  return source.length;
}

export function literalEmbeddedImages(source: string): LiteralImageReference[] {
  const references: LiteralImageReference[] = [];
  for (const match of source.matchAll(/@Image\s*\(/g)) {
    const start = match.index ?? 0;
    const end = directiveEnd(source, source.indexOf("(", start));
    const parameters = source.slice(start, end);
    if (/\bembed\s*:\s*false\b/.test(parameters)) continue;
    const image = /\bsrc\s*:\s*("(?:\\.|[^"\\])*")/.exec(parameters);
    if (!image) continue;
    try {
      const url = JSON.parse(image[1]) as string;
      if (!/^https?:\/\//i.test(url)) continue;
      const valueStart = start + (image.index ?? 0) + image[0].indexOf(image[1]);
      references.push({
        url,
        start: valueStart,
        end: valueStart + image[1].length,
      });
    } catch {
      continue;
    }
  }
  return references;
}
