import * as vscode from "vscode";

import {
  buildWorkspace,
  maximumVirtualSources,
  type BuildOptions,
  type BuiltWorkspace,
} from "./workspaceModel";

export {
  libraryRoot,
  maximumVirtualSources,
  outputContextFor,
  parentPath,
  virtualPath,
  type BuildOptions,
  type BuiltWorkspace,
  type EntryRoles,
  type ProjectConfig,
} from "./workspaceModel";

const supportedSourcePath = /\.(em|emt)$/u;
const ignoredProjectDirectories = new Set(["generated", "node_modules"]);
const packagedLibraryFiles = ["builtins.em", "engines/django.emt"];

export function compilerPathForUri(uri: vscode.Uri): string {
  return `/${vscode.workspace.asRelativePath(uri, false).replace(/^\/+/u, "")}`;
}

/**
 * The workspace's Email Markup sources as the compiler sees them: read once at
 * activation and then kept current, so a file added, changed or deleted while
 * the editor is open is reflected without a reload.
 */
export class EmailMarkupProject implements vscode.Disposable {
  private readonly files = new Map<string, string>();
  private readonly jsonFiles = new Map<string, string>();
  private readonly disposables: vscode.Disposable[] = [];
  private readonly changed = new vscode.EventEmitter<void>();
  private reportedTruncation = "";
  private version = 0;

  /** Fires when the file set changed and open documents should be re-analyzed. */
  readonly onDidChange = this.changed.event;

  constructor(
    private readonly extensionUri: vscode.Uri,
    private readonly output: vscode.LogOutputChannel,
  ) {}

  dispose(): void {
    for (const disposable of this.disposables) disposable.dispose();
    this.changed.dispose();
  }

  /** Bumped whenever the file set changes, so derived data can be cached against it. */
  get revision(): number {
    return this.version;
  }

  /** Every `.em`/`.emt` source currently known to the project. */
  sources(): ReadonlyMap<string, string> {
    return this.files;
  }

  hasJson(path: string): boolean {
    return this.jsonFiles.has(path);
  }

  setJson(path: string, source: string): void {
    if (this.jsonFiles.get(path) === source) return;
    this.jsonFiles.set(path, source);
    this.version += 1;
  }

  setSource(path: string, source: string): void {
    if (this.files.get(path) === source) return;
    this.files.set(path, source);
    this.version += 1;
  }

  async load(): Promise<void> {
    this.files.clear();
    this.jsonFiles.clear();
    const uris: vscode.Uri[] = [];

    const visit = async (directory: vscode.Uri): Promise<void> => {
      if (uris.length >= maximumVirtualSources) return;
      let entries: [string, vscode.FileType][];
      try {
        entries = await vscode.workspace.fs.readDirectory(directory);
      } catch (error) {
        this.output.warn(`Skipped ${directory.toString(true)}: ${String(error)}`);
        return;
      }
      for (const [name, type] of entries) {
        if (uris.length >= maximumVirtualSources) return;
        const uri = vscode.Uri.joinPath(directory, name);
        if ((type & vscode.FileType.Directory) !== 0) {
          if (!ignoredProjectDirectories.has(name)) await visit(uri);
        } else if (
          (type & vscode.FileType.File) !== 0 &&
          (supportedSourcePath.test(uri.path) || name.endsWith(".json"))
        ) {
          uris.push(uri);
        }
      }
    };

    await Promise.all((vscode.workspace.workspaceFolders ?? []).map(({ uri }) => visit(uri)));
    await Promise.all(uris.map((uri) => this.read(uri)));
    await this.loadPackagedLibrary();
    this.version += 1;
    this.output.info(`Loaded ${this.files.size} bounded virtual project files.`);
  }

  private async read(uri: vscode.Uri): Promise<void> {
    try {
      const source = new TextDecoder("utf-8", { fatal: true }).decode(
        await vscode.workspace.fs.readFile(uri),
      );
      const path = compilerPathForUri(uri);
      if (supportedSourcePath.test(uri.path)) this.files.set(path, source);
      else this.jsonFiles.set(path, source);
    } catch (error) {
      this.output.warn(`Skipped ${uri.toString(true)}: ${String(error)}`);
    }
  }

  private async loadPackagedLibrary(): Promise<void> {
    await Promise.all(
      packagedLibraryFiles.map(async (relative) => {
        const uri = vscode.Uri.joinPath(this.extensionUri, "browser", "lib", relative);
        const response = await fetch(uri.toString(true));
        if (!response.ok) throw new Error(`Could not load packaged library file ${relative}.`);
        this.files.set(`${"/.email-markup/lib"}/${relative}`, await response.text());
      }),
    );
  }

  /**
   * Keep the file set current. Without this the compiler only ever sees the
   * project as it stood at activation: a component added afterwards does not
   * exist as far as any open template is concerned, and the only way to correct
   * that is a page reload — which, in the hosted editor, ends the session.
   */
  watch(): void {
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.{em,emt,json}");
    const ignored = (uri: vscode.Uri): boolean =>
      uri.path.split("/").some((segment) => ignoredProjectDirectories.has(segment));
    const forget = (uri: vscode.Uri): void => {
      if (ignored(uri)) return;
      const path = compilerPathForUri(uri);
      this.files.delete(path);
      this.jsonFiles.delete(path);
      this.version += 1;
      this.changed.fire();
    };
    const refresh = async (uri: vscode.Uri): Promise<void> => {
      if (ignored(uri)) return;
      await this.read(uri);
      this.version += 1;
      this.changed.fire();
    };
    this.disposables.push(
      watcher,
      watcher.onDidCreate((uri) => void refresh(uri)),
      watcher.onDidChange((uri) => void refresh(uri)),
      watcher.onDidDelete(forget),
    );
  }

  build(entryPath: string, entrySource: string, options: BuildOptions = {}): BuiltWorkspace {
    this.setSource(entryPath, entrySource);
    const built = buildWorkspace(
      { files: this.files, json: this.jsonFiles },
      entryPath,
      entrySource,
      options,
      (message) => this.output.warn(message),
    );
    this.reportTruncation(entryPath, built.dropped);
    return built;
  }

  /**
   * Dropping files silently turns into "the component exists but the compiler
   * says it does not", which is close to undebuggable from the editor. Say it
   * once per distinct set rather than on every keystroke.
   */
  private reportTruncation(entryPath: string, dropped: string[]): void {
    if (!dropped.length) {
      this.reportedTruncation = "";
      return;
    }
    const signature = `${entryPath}:${dropped.join(",")}`;
    if (signature === this.reportedTruncation) return;
    this.reportedTruncation = signature;
    this.output.warn(
      `The project exceeds the ${maximumVirtualSources}-file compiler limit. ` +
        `${dropped.length} file(s) were left out of the analysis of ${entryPath}, and ` +
        `anything they define will be reported as unknown: ${dropped.join(", ")}`,
    );
  }
}
