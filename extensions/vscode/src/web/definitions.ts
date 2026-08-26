/**
 * A lightweight reader for what an Email Markup document *defines*, as opposed
 * to what it renders.
 *
 * The compiler reports definitions as flat symbols (name, kind, range), which is
 * enough to list them but not enough to exercise them. Previewing a component
 * library means instantiating each component, and that needs its slots and props.
 * Rather than widen the browser protocol for it, this reads the declarations back
 * out of the source — they are a small, stable, explicitly delimited part of the
 * grammar (`@Props` and `@Slots` are raw bodies, per `browser/syntax/lexical.json`).
 *
 * Everything here is best-effort and never authoritative: a document this cannot
 * parse simply yields no definitions, and the caller falls back to describing the
 * document rather than previewing it.
 */

export interface PropDeclaration {
  name: string;
  type: string;
  defaultValue?: string;
}

export interface SlotDeclaration {
  name: string;
  required: boolean;
}

export interface ComponentDefinition {
  name: string;
  props: PropDeclaration[];
  slots: SlotDeclaration[];
}

export interface TokenDefinition {
  name: string;
  value?: string;
}

export interface DocumentDefinitions {
  components: ComponentDefinition[];
  tokens: TokenDefinition[];
  /** The document renders a `@Slot(...)` outside any component — it is a shell. */
  isShell: boolean;
  /** The document contains markup that renders on its own. */
  hasRenderableBody: boolean;
}

const componentClose = "@/DefineComponent";

/**
 * Blank out comments and escaped sigils so a scan cannot mistake their contents
 * for markup, while keeping every offset identical to the original source.
 */
export function maskNonCode(source: string): string {
  const characters = [...source];
  for (let index = 0; index < characters.length; ) {
    if (source.startsWith("@@", index)) {
      characters[index] = " ";
      characters[index + 1] = " ";
      index += 2;
    } else if (source.startsWith("@*", index)) {
      const close = source.indexOf("*@", index + 2);
      const end = close === -1 ? source.length : close + 2;
      for (let inner = index; inner < end; ++inner) {
        if (characters[inner] !== "\n") characters[inner] = " ";
      }
      index = end;
    } else if (source.startsWith("@//", index)) {
      const close = source.indexOf("\n", index + 3);
      const end = close === -1 ? source.length : close;
      for (let inner = index; inner < end; ++inner) characters[inner] = " ";
      index = end;
    } else {
      ++index;
    }
  }
  return characters.join("");
}

function blockBody(
  masked: string,
  source: string,
  keyword: string,
  from: number,
  to: number,
): string {
  const open = masked.indexOf(`@${keyword}`, from);
  if (open === -1 || open >= to) return "";
  const close = masked.indexOf(`@/${keyword}`, open);
  if (close === -1 || close >= to) return "";
  return source.slice(open + keyword.length + 1, close);
}

function parseProps(body: string): PropDeclaration[] {
  const props: PropDeclaration[] = [];
  for (const line of body.split("\n")) {
    const match =
      /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z]+)\s*(?:=\s*(.+?))?\s*$/u.exec(line);
    if (!match) continue;
    props.push({ name: match[1], type: match[2], defaultValue: match[3]?.trim() });
  }
  return props;
}

function parseSlots(body: string): SlotDeclaration[] {
  const slots: SlotDeclaration[] = [];
  for (const line of body.split("\n")) {
    const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z]+)\s*$/u.exec(line);
    if (!match) continue;
    slots.push({ name: match[1], required: match[2] === "required" });
  }
  return slots;
}

/** Ranges of every `@DefineComponent … @/DefineComponent` block. */
function componentRanges(masked: string): Array<[number, number]> {
  const ranges: Array<[number, number]> = [];
  let cursor = 0;
  for (;;) {
    const open = masked.indexOf("@DefineComponent", cursor);
    if (open === -1) break;
    const close = masked.indexOf(componentClose, open);
    if (close === -1) break;
    ranges.push([open, close]);
    cursor = close + componentClose.length;
  }
  return ranges;
}

export function readDefinitions(source: string): DocumentDefinitions {
  const masked = maskNonCode(source);
  const ranges = componentRanges(masked);

  const components: ComponentDefinition[] = [];
  for (const [open, close] of ranges) {
    const header = masked.slice(open, Math.min(close, open + 400));
    const name = /@DefineComponent\s*\(\s*name\s*:\s*"([^"]+)"/u.exec(header)?.[1];
    if (!name) continue;
    components.push({
      name,
      props: parseProps(blockBody(masked, source, "Props", open, close)),
      slots: parseSlots(blockBody(masked, source, "Slots", open, close)),
    });
  }

  const tokens: TokenDefinition[] = [];
  const tokenPattern =
    /@DefineToken\s*\(\s*name\s*:\s*"([^"]+)"\s*(?:,\s*value\s*:\s*"([^"]*)"\s*)?\)/gu;
  for (const match of masked.matchAll(tokenPattern)) {
    tokens.push({ name: match[1], value: match[2] });
  }

  // A `@Slot(...)` outside every component block belongs to the document itself,
  // which is what makes the document a shell.
  let isShell = false;
  for (const match of masked.matchAll(/@Slot\s*\(/gu)) {
    const at = match.index ?? 0;
    if (!ranges.some(([open, close]) => at > open && at < close)) {
      isShell = true;
      break;
    }
  }

  // Whatever sits outside the definitions, once the declaration-only statements
  // are removed, is content the document renders on its own.
  let remainder = masked;
  for (const [open, close] of [...ranges].reverse()) {
    remainder = remainder.slice(0, open) + remainder.slice(close + componentClose.length);
  }
  remainder = remainder
    .replace(/@DefineToken\s*\([^)]*\)\s*;?/gu, "")
    .replace(/@Include\s*\([^)]*\)\s*;?/gu, "")
    .replace(/@DefineStyle[\s\S]*?@\/DefineStyle/gu, "")
    .replace(/@Engine\s*\([^)]*\)\s*;?/gu, "");
  const hasRenderableBody = remainder.trim().length > 0;

  return { components, tokens, isShell, hasRenderableBody };
}
