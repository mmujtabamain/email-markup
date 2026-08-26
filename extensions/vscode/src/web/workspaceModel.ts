import type { BrowserWorkspace } from "./browserClient";

/**
 * Assembling the virtual workspace the browser compiler is given.
 *
 * Kept free of any editor dependency so the part that actually goes wrong can be
 * exercised directly: which files the compiler is handed, which of them are
 * passed in their own field instead, and what happens when the document you have
 * open is also the project's shell, engine or one of its imports.
 */

export const maximumVirtualSources = 252;
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
  /** Files left out to stay inside the protocol's file cap, if any. */
  dropped: string[];
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

  // A document cannot be wrapped in itself. When the entry *is* the shell or the
  // engine, it is compiled standalone unless the caller supplied another.
  const shellPath = options.shellPath ?? (roles.isShell ? undefined : configuredShell);
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

  // Ordered by how likely the entry is to need them, so that if the protocol's
  // file cap does bite, it bites the least relevant files.
  const ordered = new Map<string, string>();
  const offer = (path: string): void => {
    if (suppliedSeparately.has(path) || ordered.has(path)) return;
    const source = files.get(path);
    if (source !== undefined) ordered.set(path, source);
  };
  for (const path of imports) offer(path);
  for (const path of files.keys()) if (path.startsWith(`${libraryRoot}/`)) offer(path);
  for (const path of files.keys()) if (path.startsWith(`${root}/`)) offer(path);
  for (const path of files.keys()) offer(path);

  const entries = [...ordered.entries()];

  return {
    roles,
    dropped: entries.slice(maximumVirtualSources).map(([path]) => path),
    workspace: {
      entry_path: entryPath,
      source: entrySource,
      files: entries
        .slice(0, maximumVirtualSources)
        .map(([path, source]) => ({ path, source })),
      include_directories: includeDirectories,
      imports,
      shell,
      engine,
      data: parseObject(snapshot.json, resolve(config?.data), onWarning),
      context_schema: parseObject(snapshot.json, resolve(config?.context_schema), onWarning),
      output_context: options.outputContext ?? outputContextFor(entryPath),
    },
  };
}
