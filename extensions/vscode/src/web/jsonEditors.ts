import * as vscode from "vscode";

/**
 * Purpose-built editors for the JSON that Email Markup projects are made of.
 *
 * A context contract, a project configuration and a compiled artifact are all
 * JSON, but none of them is read the way JSON is read. A contract is a field
 * table an author consults to find the interpolation they need; a project
 * configuration is a set of paths whose most interesting property is whether they
 * resolve; a compiled artifact is build output that should not be edited at all.
 * Shown as raw text they are three walls of braces and the reader does the
 * rendering in their head.
 *
 * Each of these keeps a "View source" affordance, because a custom editor you
 * cannot escape is a trap on the day the file is malformed.
 */

const projectViewType = "email-markup.projectConfig";
const contractViewType = "email-markup.contextContract";
const artifactViewType = "email-markup.compiledArtifact";

// ---------------------------------------------------------------- shared shell

function nonce(): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

function escapeText(value: string): string {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}

function escapeAttribute(value: string): string {
  return escapeText(value).replaceAll('"', "&quot;");
}

const sharedStyle = `*{box-sizing:border-box}
body{margin:0;padding:0;background:var(--vscode-editor-background);color:var(--vscode-editor-foreground);font-family:var(--vscode-font-family);font-size:13px;line-height:1.5}
header{display:flex;align-items:center;gap:12px;padding:14px 20px;border-bottom:1px solid var(--vscode-editorWidget-border);position:sticky;top:0;background:var(--vscode-editor-background);z-index:2}
header h1{margin:0;font-size:14px;font-weight:600}
header .kind{font-size:11px;letter-spacing:.07em;text-transform:uppercase;opacity:.6}
header .spacer{flex:1}
button{font:inherit;font-size:12px;padding:4px 11px;border-radius:4px;border:1px solid var(--vscode-button-border,transparent);background:var(--vscode-button-secondaryBackground);color:var(--vscode-button-secondaryForeground);cursor:pointer}
main{padding:18px 20px 42px}
.search{width:100%;padding:6px 10px;margin-bottom:14px;border-radius:5px;border:1px solid var(--vscode-input-border,var(--vscode-editorWidget-border));background:var(--vscode-input-background);color:var(--vscode-input-foreground);font:inherit;font-size:12px}
table{width:100%;border-collapse:collapse}
th{text-align:left;font-size:11px;letter-spacing:.06em;text-transform:uppercase;opacity:.6;font-weight:600;padding:0 10px 7px;border-bottom:1px solid var(--vscode-editorWidget-border)}
td{padding:9px 10px;border-bottom:1px solid var(--vscode-editorWidget-border);vertical-align:top}
tr.hidden{display:none}
.path{font-family:var(--vscode-editor-font-family);font-size:12px;font-weight:600}
.desc{opacity:.75;font-size:12px;margin-top:3px}
.type{font-family:var(--vscode-editor-font-family);font-size:12px;opacity:.9}
.tag{display:inline-block;font-size:10.5px;padding:1px 6px;border-radius:999px;border:1px solid var(--vscode-editorWidget-border);margin:0 4px 3px 0;opacity:.85}
.tag.required{border-color:var(--vscode-textLink-foreground);color:var(--vscode-textLink-foreground);opacity:1}
.example{font-family:var(--vscode-editor-font-family);font-size:12px;opacity:.8;word-break:break-word}
.interp{font-family:var(--vscode-editor-font-family);font-size:12px;background:var(--vscode-textCodeBlock-background);border:1px solid transparent;padding:2px 7px;border-radius:4px;cursor:pointer}
.interp:hover{border-color:var(--vscode-textLink-foreground)}
.rows{display:grid;grid-template-columns:minmax(9rem,auto) 1fr;gap:9px 18px;align-items:baseline;margin:0}
.rows dt{font-size:11px;letter-spacing:.06em;text-transform:uppercase;opacity:.6}
.rows dd{margin:0}
.ref{font-family:var(--vscode-editor-font-family);font-size:12px;cursor:pointer;color:var(--vscode-textLink-foreground)}
.ref:hover{text-decoration:underline}
.ref.missing{color:var(--vscode-errorForeground);cursor:default;text-decoration:line-through}
.ref.plain{color:inherit;cursor:default}
.note{margin:0 0 16px;padding:9px 13px;border-radius:6px;background:var(--vscode-editorWidget-background);border:1px solid var(--vscode-editorWidget-border);font-size:12px}
.empty{opacity:.65;padding:14px 0}
h2{font-size:12px;letter-spacing:.06em;text-transform:uppercase;opacity:.6;font-weight:600;margin:26px 0 10px}
pre{margin:0;padding:14px;border-radius:6px;background:var(--vscode-textCodeBlock-background);overflow-x:auto;font-family:var(--vscode-editor-font-family);font-size:12px}`;

const sharedScript = `const vscode = acquireVsCodeApi();
document.addEventListener('click', (event) => {
  const source = event.target.closest('[data-source]');
  if (source) { vscode.postMessage({ type: 'viewSource' }); return; }
  const open = event.target.closest('[data-open]');
  if (open) { vscode.postMessage({ type: 'open', path: open.dataset.open }); return; }
  const copy = event.target.closest('[data-copy]');
  if (copy) { vscode.postMessage({ type: 'copy', text: copy.dataset.copy }); }
});
const search = document.querySelector('.search');
if (search) {
  search.addEventListener('input', () => {
    const needle = search.value.trim().toLowerCase();
    for (const row of document.querySelectorAll('tbody tr')) {
      row.classList.toggle('hidden', needle !== '' && !row.dataset.haystack.includes(needle));
    }
  });
}`;

function shell(kind: string, title: string, body: string): string {
  const scriptNonce = nonce();
  return `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${scriptNonce}'">
<style>${sharedStyle}</style></head><body>
<header><span class="kind">${escapeText(kind)}</span><h1>${escapeText(title)}</h1>
<span class="spacer"></span><button data-source>View source</button></header>
<main>${body}</main>
<script nonce="${scriptNonce}">${sharedScript}</script></body></html>`;
}

function invalidJson(kind: string, title: string, error: unknown): string {
  return shell(
    kind,
    title,
    `<p class="note">This file is not valid JSON, so it cannot be rendered. Open the source to fix
it — the JSON language service will point at the problem.</p>
<pre>${escapeText(error instanceof Error ? error.message : String(error))}</pre>`,
  );
}

/** Wire the affordances every one of these editors offers. */
function connect(panel: vscode.WebviewPanel, document: vscode.TextDocument): vscode.Disposable {
  return panel.webview.onDidReceiveMessage(
    async (message: { type: string; path?: string; text?: string }) => {
      if (message.type === "viewSource") {
        await vscode.commands.executeCommand("vscode.openWith", document.uri, "default");
        return;
      }
      if (message.type === "copy" && message.text) {
        await vscode.env.clipboard.writeText(message.text);
        void vscode.window.setStatusBarMessage(`Copied ${message.text}`, 2000);
        return;
      }
      if (message.type === "open" && message.path) {
        const target = vscode.Uri.joinPath(document.uri, "..", message.path);
        try {
          await vscode.window.showTextDocument(target);
        } catch {
          void vscode.window.showWarningMessage(`${message.path} could not be opened.`);
        }
      }
    },
  );
}

// ---------------------------------------------------------------- context contract

interface SchemaField {
  type?: string;
  required?: boolean;
  nullable?: boolean;
  description?: string;
  example?: unknown;
  minimum?: number;
  maximum?: number;
  minLength?: number;
  maxLength?: number;
  fields?: Record<string, SchemaField>;
  items?: SchemaField;
}

function constraints(field: SchemaField): string[] {
  const values: string[] = [];
  if (field.required) values.push("required");
  if (field.nullable) values.push("nullable");
  if (field.minimum !== undefined) values.push(`min ${field.minimum}`);
  if (field.maximum !== undefined) values.push(`max ${field.maximum}`);
  if (field.minLength !== undefined) values.push(`min length ${field.minLength}`);
  if (field.maxLength !== undefined) values.push(`max length ${field.maxLength}`);
  return values;
}

function contractRows(fields: Record<string, SchemaField>, prefix = "", depth = 0): string {
  return Object.entries(fields)
    .map(([name, field]) => {
      const path = prefix ? `${prefix}.${name}` : name;
      const nested = field.fields ?? field.items?.fields;
      const tags = constraints(field)
        .map(
          (value) =>
            `<span class="tag${value === "required" ? " required" : ""}">${escapeText(value)}</span>`,
        )
        .join("");
      const interpolation = `@[${path}]`;
      const haystack = `${path} ${field.type ?? ""} ${field.description ?? ""}`.toLowerCase();
      const row = `<tr data-haystack="${escapeAttribute(haystack)}">
  <td style="padding-left:${10 + depth * 16}px">
    <div class="path">${escapeText(path)}</div>
    ${field.description ? `<div class="desc">${escapeText(field.description)}</div>` : ""}
  </td>
  <td><span class="type">${escapeText(field.type ?? "—")}</span></td>
  <td>${tags || '<span style="opacity:.5">—</span>'}</td>
  <td><span class="example">${
    field.example === undefined ? "—" : escapeText(JSON.stringify(field.example))
  }</span></td>
  <td><span class="interp" data-copy="${escapeAttribute(interpolation)}" title="Copy">${escapeText(
    interpolation,
  )}</span></td>
</tr>`;
      return nested ? row + contractRows(nested, path, depth + 1) : row;
    })
    .join("\n");
}

function renderContract(document: vscode.TextDocument): string {
  const name = document.uri.path.split("/").pop() ?? "context contract";
  let parsed: { name?: string; version?: number; fields?: Record<string, SchemaField> };
  try {
    parsed = JSON.parse(document.getText()) as typeof parsed;
  } catch (error) {
    return invalidJson("Context contract", name, error);
  }
  const fields = parsed.fields ?? {};
  const body = Object.keys(fields).length
    ? `<p class="note">Every value a template may interpolate, and the shape publication validates
against. Click an interpolation to copy it.</p>
<input class="search" type="search" placeholder="Filter fields by name, type or description" aria-label="Filter fields">
<table><thead><tr><th>Field</th><th>Type</th><th>Constraints</th><th>Example</th><th>Interpolation</th></tr></thead>
<tbody>${contractRows(fields)}</tbody></table>`
    : `<p class="empty">This contract declares no fields.</p>`;
  return shell(
    "Context contract",
    `${parsed.name ?? name}${parsed.version === undefined ? "" : ` · version ${parsed.version}`}`,
    body,
  );
}

// ---------------------------------------------------------------- project config

const projectFields: Array<[string, string]> = [
  ["shell", "Wraps every compiled template"],
  ["engine", "Turns compiled markup into target-language template source"],
  ["context_schema", "The contract templates interpolate against"],
  ["data", "Development fixture used for local preview"],
  ["out", "Where compiled output is written"],
];

/**
 * Whether a configured path resolves, asked without `stat`.
 *
 * Growth Console's file system provider acquires a *file lease* inside `stat`
 * for anything writable, so checking a dozen paths that way meant a dozen
 * network round trips and a dozen files locked for an author who was only
 * looking at `em.json`. Listing the parent directory answers the same question
 * from the provider's cached tree, takes no lease, and is one request per
 * directory rather than one per file.
 */
class DirectoryIndex {
  private readonly listings = new Map<string, Promise<Set<string>>>();

  constructor(private readonly base: vscode.Uri) {}

  private entries(directory: string): Promise<Set<string>> {
    let listing = this.listings.get(directory);
    if (!listing) {
      listing = (async () => {
        try {
          const read = await vscode.workspace.fs.readDirectory(
            directory ? vscode.Uri.joinPath(this.base, directory) : this.base,
          );
          return new Set(read.map(([name]) => name));
        } catch {
          return new Set<string>();
        }
      })();
      this.listings.set(directory, listing);
    }
    return listing;
  }

  async resolves(value: string): Promise<boolean> {
    // The packaged library is not part of the repository and always resolves.
    if (value.includes("${EMAIL_MARKUP_LIB}")) return true;
    const parts = value.split("/").filter((part) => part && part !== ".");
    if (!parts.length) return false;
    const name = parts.pop() as string;
    return (await this.entries(parts.join("/"))).has(name);
  }
}

function reference(value: string, exists: boolean): string {
  if (value.includes("${EMAIL_MARKUP_LIB}")) {
    return `<span class="ref plain" title="Resolved from the packaged Email Markup library">${escapeText(
      value,
    )}</span>`;
  }
  return exists
    ? `<span class="ref" data-open="${escapeAttribute(value)}">${escapeText(value)}</span>`
    : `<span class="ref missing" title="This path does not resolve">${escapeText(value)}</span>`;
}

async function renderProject(document: vscode.TextDocument): Promise<string> {
  const name = document.uri.path.split("/").pop() ?? "em.json";
  const index = new DirectoryIndex(vscode.Uri.joinPath(document.uri, ".."));
  let parsed: Record<string, unknown>;
  try {
    parsed = JSON.parse(document.getText()) as Record<string, unknown>;
  } catch (error) {
    return invalidJson("Project", name, error);
  }

  const list = async (values: unknown, label: string): Promise<string> => {
    const items = Array.isArray(values)
      ? values.filter((item): item is string => typeof item === "string")
      : [];
    if (!items.length) return `<p class="empty">No ${label}.</p>`;
    const rendered = await Promise.all(
      items.map(async (item) => `<li>${reference(item, await index.resolves(item))}</li>`),
    );
    return `<ul style="margin:0;padding-left:18px">${rendered.join("")}</ul>`;
  };

  const singles = await Promise.all(
    projectFields.map(async ([key, description]) => {
      const value = parsed[key];
      if (typeof value !== "string") return "";
      const rendered =
        key === "out"
          ? `<span class="ref plain">${escapeText(value)}</span>`
          : reference(value, await index.resolves(value));
      return `<dt>${escapeText(key)}</dt><dd>${rendered}<div class="desc">${escapeText(
        description,
      )}</div></dd>`;
    }),
  );

  return shell(
    "Project",
    name,
    `<p class="note">What this project binds together. A path that does not resolve is shown struck
through — that is usually why a template will not compile.</p>
<dl class="rows">${singles.join("")}</dl>
<h2>Imports · in scope for every document</h2>${await list(parsed.imports, "imports")}
<h2>Include directories · searched by @Include</h2>${await list(
      parsed.include,
      "include directories",
    )}`,
  );
}

// ---------------------------------------------------------------- compiled artifact

function renderArtifact(document: vscode.TextDocument): string {
  const segments = document.uri.path.split("/").filter(Boolean);
  const name = segments.at(-1) ?? "artifact.json";
  let parsed: unknown;
  try {
    parsed = JSON.parse(document.getText());
  } catch (error) {
    return invalidJson("Compiled artifact", name, error);
  }
  const summary =
    parsed && typeof parsed === "object" && !Array.isArray(parsed)
      ? Object.entries(parsed as Record<string, unknown>)
          .filter(([, value]) => typeof value !== "object")
          .map(
            ([key, value]) =>
              `<dt>${escapeText(key)}</dt><dd class="example">${escapeText(String(value))}</dd>`,
          )
          .join("")
      : "";
  return shell(
    "Compiled artifact",
    `${segments.at(-2) ?? ""}${segments.at(-2) ? " · " : ""}${name}`,
    `<p class="note">Build output. It is regenerated from the sources on every compile, so editing it
here has no effect — change the template it came from instead.</p>
${summary ? `<dl class="rows">${summary}</dl>` : ""}
<h2>Contents</h2><pre>${escapeText(JSON.stringify(parsed, null, 2))}</pre>`,
  );
}

// ---------------------------------------------------------------- registration

function provider(
  render: (document: vscode.TextDocument) => string | Promise<string>,
): vscode.CustomTextEditorProvider {
  return {
    async resolveCustomTextEditor(document, panel) {
      panel.webview.options = { enableScripts: true };
      const messages = connect(panel, document);
      panel.onDidDispose(() => messages.dispose());
      // Rendered once, when the document is opened. These views describe a
      // project rather than watching one, and re-rendering the whole webview on
      // every keystroke bought nothing — reopen the tab to see a change.
      panel.webview.html = await render(document);
    },
  };
}

export function registerJsonEditors(): vscode.Disposable[] {
  const options = { webviewOptions: { retainContextWhenHidden: false } };
  return [
    vscode.window.registerCustomEditorProvider(contractViewType, provider(renderContract), options),
    vscode.window.registerCustomEditorProvider(projectViewType, provider(renderProject), options),
    vscode.window.registerCustomEditorProvider(artifactViewType, provider(renderArtifact), options),
  ];
}
