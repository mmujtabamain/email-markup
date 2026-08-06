export function cssProjection(source: string): { text: string; ranges: Array<[number, number]> } {
  const characters: string[] = source.split("").map((character) => character === "\n" ? "\n" : " ");
  const ranges: Array<[number, number]> = [];
  const pattern = /<style(?:\s[^>]*)?>([\s\S]*?)<\/style\s*>/gi;
  for (const match of source.matchAll(pattern)) {
    const body = match[1];
    const start = (match.index ?? 0) + match[0].indexOf(body);
    const end = start + body.length;
    ranges.push([start, end]);
    for (let index = start; index < end; ++index) characters[index] = source[index];
  }
  return { text: characters.join(""), ranges };
}

export function inRanges(offset: number, ranges: Array<[number, number]>): boolean {
  return ranges.some(([start, end]) => offset >= start && offset <= end);
}

export function localClasses(source: string): string[] {
  const names = new Set<string>();
  const projection = cssProjection(source).text;
  for (const match of projection.matchAll(/\.([_a-zA-Z][_a-zA-Z0-9-]*)/g)) names.add(match[1]);
  return [...names].sort();
}
