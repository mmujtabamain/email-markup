import type { BrowserWorkspace } from "./browserClient";
import { readDefinitions } from "./definitions";

/**
 * Assembling the virtual workspace the browser compiler is given.
 *
 * Kept free of any editor dependency so the part that actually goes wrong can be
 * exercised directly: which files the compiler is handed, which of them are
 * passed in their own field instead, and what happens when the document you have
 * open is also the project's shell, engine or one of its imports.
 */

export const maximumVirtualSources = 252;

/**
 * A ceiling on the project handed to the compiler, in bytes.
 *
 * This is a backstop rather than the real control. The browser compiler parses
 * on WebAssembly's stack, 64 KiB by default, and exhausting it does not produce
 * an error — it faults, and the fault reaches the author as "WebAssembly
 * exception 225832" with no file, no line and no cause. What exhausts it is
 * parsing depth rather than volume: forty kilobytes of comments compile
 * happily, while a dozen real templates with nested markup do not.
 *
 * So the real control is `isReachableFrom` below — not sending documents the
 * entry could not refer to even in principle. This limit only catches a project
 * that is large in the reachable part too.
 */
export const maximumWorkspaceBytes = 96 * 1024;
export const libraryRoot = "/.email-markup/lib";

export interface ProjectConfig {
  include?: unknown;
  imports?: unknown;
  data?: unknown;
  context_schema?: unknown;
  shell?: unknown;
  engine?: unknown;
  out?: unknown;
}

/** How a document was reached, which decides what a preview of it should be. */
export interface EntryRoles {
  /** The document is the project's configured shell. */
  isShell: boolean;
  /** The document is the project's configured engine. */
  isEngine: boolean;
  /** The document is named by the project's `imports`. */
  isImported: boolean;
}

export interface BuildOptions {
  /** Override the shell the workspace is compiled against. */
  shellPath?: string;
  /** Compile for a subject line rather than an HTML body. */
  outputContext?: "html" | "subject";
  /** Bring extra files into scope — used by synthetic preview documents. */
  extraImports?: string[];
}

export interface BuiltWorkspace {
  /** Always carries the collections the assembler fills in, so callers need no guards. */
  workspace: BrowserWorkspace &
    Required<Pick<BrowserWorkspace, "files" | "imports" | "include_directories">>;
  roles: EntryRoles;
  /** Files left out to stay inside a limit — worth telling the author about. */
  dropped: string[];
  /**
   * Files the entry could not refer to even in principle, so not sent.
   *
   * Distinct from `dropped` on purpose: nothing is lost by leaving these out,
   * and warning about them would be noise. If one of them really was meant to
   * be reachable, the compiler says so precisely — "cannot resolve" — which is
   * a better message than anything this could invent.
   */
  unreachable: string[];
}

export interface ProjectSnapshot {
  /** Every `.em`/`.emt` source, keyed by virtual path. */
  files: ReadonlyMap<string, string>;
  /** Every `.json` source, keyed by virtual path. */
  json: ReadonlyMap<string, string>;
}

export function virtualPath(base: string, value: string): string | undefined {
  const expanded = value.replaceAll("${EMAIL_MARKUP_LIB}", libraryRoot);
  const parts = expanded.startsWith("/") ? [] : base.split("/").filter(Boolean);
  for (const part of expanded.split("/")) {
    if (!part || part === ".") continue;
    if (part === "..") {
      if (!parts.length) return undefined;
      parts.pop();
    } else {
      parts.push(part);
    }
  }
  return `/${parts.join("/")}`;
}

export function parentPath(path: string): string {
  return path.slice(0, path.lastIndexOf("/")) || "/";
}

/**
 * `subject.em` compiles to a header line, not a document. The convention is the
 * project's own — `growth-console-emails/tools/check.py` and Growth Console's
 * compiler both pair `body.em` with `subject.em` — and the browser protocol has
 * carried `output_context` for it all along.
 */
export function outputContextFor(path: string): "html" | "subject" {
  return /(^|\/)subject\.em$/u.test(path) ? "subject" : "html";
}

/** UTF-8 size, because the limit is bytes on the wire, not characters. */
function byteLength(value: string): number {
  return new TextEncoder().encode(value).length;
}

function stringArray(value: unknown): string[] {
  return Array.isArray(value)
    ? value.filter((item): item is string => typeof item === "string")
    : [];
}

/** The nearest `em.json` at or above the entry, and the directory it sits in. */
export function locateConfig(
  json: ReadonlyMap<string, string>,
  entryPath: string,
  onWarning: (message: string) => void,
): { config?: ProjectConfig; root: string } {
  const configPath = [...json.keys()]
    .filter((path) => {
      const root = parentPath(path);
      return (
        path.endsWith("/em.json") &&
        (root === "/" ? entryPath.startsWith("/") : entryPath.startsWith(`${root}/`))
      );
    })
    .sort((left, right) => right.length - left.length)[0];
  if (!configPath) return { root: parentPath(entryPath) };
  try {
    const parsed: unknown = JSON.parse(json.get(configPath) ?? "");
    if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
      return { config: parsed as ProjectConfig, root: parentPath(configPath) };
    }
  } catch (error) {
    onWarning(`Ignored invalid ${configPath}: ${String(error)}`);
  }
  return { root: parentPath(configPath) };
}

function parseObject(
  json: ReadonlyMap<string, string>,
  path: string | undefined,
  onWarning: (message: string) => void,
): Record<string, unknown> | undefined {
  if (!path) return undefined;
  try {
    const value: unknown = JSON.parse(json.get(path) ?? "");
    return value && typeof value === "object" && !Array.isArray(value)
      ? (value as Record<string, unknown>)
      : undefined;
  } catch (error) {
    onWarning(`Ignored invalid ${path}: ${String(error)}`);
    return undefined;
  }
}

/**
 * Assemble the workspace for one entry document.
 *
 * The delicate part is that the entry, the shell and the engine are each passed
 * to the compiler in their own field, so none of them may *also* appear in the
 * ordinary file set — but one document can hold more than one of those roles at
 * once. A project whose `shell` is the file you have open used to be handed that
 * document as both the entry and the shell to wrap it in, so the shell was asked
 * to embed itself and the compiler ran out of stack; a document named in
 * `imports` was removed from the file set while its path stayed in the import
 * list, so the compiler was told to import a file that was not there. Both are
 * resolved by reconciling the entry's identity with its roles before anything
 * else is decided.
 */
export function buildWorkspace(
  snapshot: ProjectSnapshot,
  entryPath: string,
  entrySource: string,
  options: BuildOptions = {},
  onWarning: (message: string) => void = () => {},
): BuiltWorkspace {
  const files = new Map(snapshot.files);
  files.set(entryPath, entrySource);

  const { config, root } = locateConfig(snapshot.json, entryPath, onWarning);
  const resolve = (value: unknown): string | undefined =>
    typeof value === "string" ? virtualPath(root, value) : undefined;

  const declaredImports = config
    ? stringArray(config.imports)
        .map(resolve)
        .filter((path): path is string => Boolean(path))
    : [`${libraryRoot}/builtins.em`];
  const includeDirectories = config
    ? stringArray(config.include)
        .map(resolve)
        .filter((path): path is string => Boolean(path))
    : [libraryRoot];
  const configuredShell = resolve(config?.shell);
  const configuredEngine = resolve(config?.engine);

  const roles: EntryRoles = {
    isShell: configuredShell === entryPath,
    isEngine: configuredEngine === entryPath,
    isImported: declaredImports.includes(entryPath),
  };

  const outputContext = options.outputContext ?? outputContextFor(entryPath);
  // A shell frames a *message*. Three kinds of document are not one, and
  // wrapping them in it produces nonsense or worse:
  //
  //  - the shell itself, which would be asked to embed itself;
  //  - a subject line, which must stay one header-safe line and instead came
  //    back wrapped in a complete HTML document, failing its own length rule;
  //  - a component or token library, which renders nothing to frame. Wrapping
  //    `components/notice.em` faulted the compiler outright, and wrapping
  //    `styles/project.em` made the shell's own `@Include("project.em")`
  //    unresolvable, because the entry is held out of the file set.
  const definitions = readDefinitions(entrySource);
  const definitionsOnly =
    !definitions.hasRenderableBody &&
    (definitions.components.length > 0 || definitions.tokens.length > 0);
  const wantsShell = outputContext !== "subject" && !definitionsOnly;

  // And a document cannot be wrapped in itself. When the entry *is* the shell or
  // the engine, it is compiled standalone unless the caller supplied another.
  const shellPath = options.shellPath ?? (roles.isShell || !wantsShell ? undefined : configuredShell);
  const enginePath = roles.isEngine ? undefined : configuredEngine;

  const shell =
    shellPath && shellPath !== entryPath && files.has(shellPath)
      ? { path: shellPath, source: files.get(shellPath) ?? "" }
      : undefined;
  const engine =
    enginePath && enginePath !== entryPath && files.has(enginePath)
      ? { path: enginePath, source: files.get(enginePath) ?? "" }
      : undefined;

  // Everything handed over in its own field is kept out of the file set, and
  // never asked for again through `imports`.
  const suppliedSeparately = new Set(
    [entryPath, shell?.path, engine?.path].filter((path): path is string => Boolean(path)),
  );
  const imports = [...declaredImports, ...(options.extraImports ?? [])].filter(
    (path, index, all) => !suppliedSeparately.has(path) && all.indexOf(path) === index,
  );

  /**
   * Whether the entry could refer to a document at all.
   *
   * `@Include` resolves against the include directories and nothing else, so a
   * file that is neither imported, nor inside one of those directories, nor
   * beside the entry itself, cannot be named by it. Sending one anyway costs
   * the compiler a full parse for something that can never be referenced — and
   * that parse is what exhausts the stack. In this project it meant every
   * template was parsed to analyze any other template.
   */
  const entryDirectory = parentPath(entryPath);
  const isReachableFrom = (path: string): boolean =>
    imports.includes(path) ||
    path === shell?.path ||
    path === engine?.path ||
    parentPath(path) === entryDirectory ||
    includeDirectories.some(
      (directory) => path.startsWith(`${directory}/`) || parentPath(path) === directory,
    );

  // Ordered by how likely the entry is to need them, so that if a limit does
  // bite, it bites the least relevant files first.
  const ordered = new Map<string, string>();
  const unreachable: string[] = [];
  const offer = (path: string): void => {
    if (suppliedSeparately.has(path) || ordered.has(path)) return;
    const source = files.get(path);
    if (source === undefined) return;
    if (!isReachableFrom(path)) {
      if (!unreachable.includes(path)) unreachable.push(path);
      return;
    }
    ordered.set(path, source);
  };
  for (const path of imports) offer(path);
  for (const path of files.keys()) if (path.startsWith(`${libraryRoot}/`)) offer(path);
  for (const path of files.keys()) if (path.startsWith(`${root}/`)) offer(path);
  for (const path of files.keys()) offer(path);

  // Take files in priority order until either limit is reached. The entry's own
  // source is already spoken for, so it is charged against the budget first.
  const kept: Array<[string, string]> = [];
  const dropped: string[] = [];
  let budget =
    maximumWorkspaceBytes -
    byteLength(entrySource) -
    byteLength(shell?.source ?? "") -
    byteLength(engine?.source ?? "");
  for (const [path, source] of ordered) {
    const cost = byteLength(path) + byteLength(source);
    if (kept.length >= maximumVirtualSources || cost > budget) {
      dropped.push(path);
      continue;
    }
    budget -= cost;
    kept.push([path, source]);
  }

  return {
    roles,
    dropped,
    unreachable,
    workspace: {
      entry_path: entryPath,
      source: entrySource,
      files: kept.map(([path, source]) => ({ path, source })),
      include_directories: includeDirectories,
      imports,
      shell,
      engine,
      data: parseObject(snapshot.json, resolve(config?.data), onWarning),
      context_schema: parseObject(snapshot.json, resolve(config?.context_schema), onWarning),
      output_context: outputContext,
    },
  };
}
