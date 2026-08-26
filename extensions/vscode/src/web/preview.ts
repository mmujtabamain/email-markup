import type { AnalyzeResult } from "./browserClient";
import type {
  ComponentDefinition,
  DocumentDefinitions,
  PropDeclaration,
  TokenDefinition,
} from "./definitions";
import type { EntryRoles } from "./project";

/**
 * What a preview of the open document should actually show.
 *
 * Not every Email Markup document renders. A component library, a token sheet
 * and a shell each define something for other documents to use, so the compiler
 * returns no preview for them — which is correct, and used to be reported as
 * "the document has compiler errors" whether or not there were any. Each of those
 * has an obvious useful preview, reached by compiling a small synthetic document
 * that exercises the definitions rather than by asking the compiler for a new
 * capability.
 */
export type PreviewPlan =
  | { kind: "document"; outputContext: "html" | "subject" }
  | { kind: "shell" }
  | { kind: "gallery"; components: ComponentDefinition[] }
  | { kind: "tokens"; tokens: TokenDefinition[] }
  | { kind: "blocked"; errors: number }
  | { kind: "none"; headline: string; detail: string };

export const syntheticEntryName = "__email-markup-preview__.em";

function errorCount(result: AnalyzeResult): number {
  return result.diagnostics.filter((item) => item.severity === "error").length;
}

/**
 * Decide what to show, consulting what the compiler actually reported rather
 * than treating a missing preview as a failure.
 */
export function planPreview(
  entryPath: string,
  result: AnalyzeResult,
  definitions: DocumentDefinitions,
  roles: EntryRoles,
  outputContext: "html" | "subject",
): PreviewPlan {
  const errors = errorCount(result);
  if (errors > 0) return { kind: "blocked", errors };
  if (result.preview) return { kind: "document", outputContext };

  if (roles.isShell || (definitions.isShell && !definitions.hasRenderableBody)) {
    return { kind: "shell" };
  }
  if (definitions.components.length && !definitions.hasRenderableBody) {
    return { kind: "gallery", components: definitions.components };
  }
  if (definitions.tokens.length && !definitions.hasRenderableBody) {
    return { kind: "tokens", tokens: definitions.tokens };
  }
  if (result.output_kind === "engine-definition") {
    return {
      kind: "none",
      headline: "This file defines the output engine",
      detail:
        "An engine describes how compiled markup is turned into target-language template " +
        "source. It has no rendered output of its own — open a template to see the engine applied.",
    };
  }
  return {
    kind: "none",
    headline: "Nothing to render",
    detail:
      `${entryPath} compiled without errors but produced no output. That is expected for a ` +
      "file that only defines components, tokens or styles for other documents to use.",
  };
}

// ---------------------------------------------------------------- synthetic sources

function placeholderProp(prop: PropDeclaration): string {
  if (prop.defaultValue) return `${prop.name}: ${prop.defaultValue}`;
  const literal = ((): string => {
    switch (prop.type) {
      case "int":
        return "12";
      case "decimal":
      case "number":
        return "3.5";
      case "bool":
        return "true";
      case "color":
        return '"#8E51D0"';
      case "url":
        return '"https://example.invalid/preview"';
      case "email":
        return '"preview@example.invalid"';
      case "name":
        return '"preview"';
      default:
        return `"Sample ${prop.name.replaceAll("_", " ")}"`;
    }
  })();
  return `${prop.name}: ${literal}`;
}

function placeholderSlotBody(name: string): string {
  return name === "default"
    ? "Placeholder slot content, so the component renders the way it will in an email."
    : `Placeholder content for the <strong>${name}</strong> slot.`;
}

function componentUsage(component: ComponentDefinition): string {
  const props = component.props.map(placeholderProp).join(", ");
  const call = props ? `@${component.name}(${props})` : `@${component.name}`;
  if (!component.slots.length) return `${call};`;

  const body = component.slots
    .map((slot) =>
      slot.name === "default"
        ? `  ${placeholderSlotBody("default")}`
        : `  @Slot(${slot.name}) ${placeholderSlotBody(slot.name)} @/Slot`,
    )
    .join("\n");
  return `${call}\n${body}\n@/${component.name}`;
}

/**
 * A document that instantiates every component the open file defines. The file
 * itself is brought into scope through the workspace's `imports` rather than an
 * `@Include`, so this works whether or not the project's include directories
 * happen to cover where the component lives.
 */
export function galleryEntry(components: ComponentDefinition[]): string {
  const sections = components
    .map(
      (component) => `<p style="margin:22px 0 6px;font:600 11px/1.4 Arial,sans-serif;letter-spacing:.08em;text-transform:uppercase;color:#6b6478;">${component.name}</p>
${componentUsage(component)}`,
    )
    .join("\n");
  return `${sections}\n`;
}

/** A document that renders every token the open file defines as a swatch. */
export function tokenEntry(tokens: TokenDefinition[]): string {
  const rows = tokens
    .map(
      (token) => `    <tr>
      <td style="width:60px;padding:7px 8px;"><div style="width:46px;height:28px;border-radius:5px;border:1px solid rgba(0,0,0,.18);background:@{token.${token.name}};"></div></td>
      <td style="padding:7px 8px;font:600 13px/1.4 Arial,sans-serif;">${token.name}</td>
      <td style="padding:7px 8px;font:12px/1.4 Consolas,monospace;color:#574f66;">@{token.${token.name}}</td>
      <td style="padding:7px 8px;"><span style="display:inline-block;padding:4px 10px;border-radius:4px;background:@{token.${token.name}};color:#ffffff;font:600 12px/1.4 Arial,sans-serif;">On colour</span></td>
    </tr>`,
    )
    .join("\n");
  return `<div style="padding:20px;font-family:Arial,sans-serif;">
  <table role="presentation" cellpadding="0" cellspacing="0" border="0" style="border-collapse:collapse;width:100%;">
${rows}
  </table>
</div>
`;
}

/** A representative body, so a shell can be previewed with something inside it. */
export function shellBodyEntry(): string {
  return `<h1 style="margin:0 0 12px;font:600 22px/1.3 Arial,sans-serif;">Shell preview</h1>
<p style="margin:0 0 12px;">This placeholder body shows how the shell frames a message: the header,
spacing, background and footer are the shell's, and everything between them comes from the
template.</p>
<p style="margin:0;">Open a template to preview real content in this shell.</p>
`;
}

// ---------------------------------------------------------------- webview rendering

function escapeText(value: string): string {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}

function escapeAttribute(value: string): string {
  return value.replaceAll("&", "&amp;").replaceAll('"', "&quot;");
}

const documentStyle = `body{margin:0;background:var(--vscode-editor-background);color:var(--vscode-editor-foreground);font-family:var(--vscode-font-family);font-size:13px;line-height:1.55}
.state{display:flex;align-items:center;gap:8px;padding:8px 14px;background:var(--vscode-editorWidget-background);color:var(--vscode-editorWidget-foreground);border-bottom:1px solid var(--vscode-editorWidget-border);font-weight:600;font-size:12px}
.state .dot{width:7px;height:7px;border-radius:50%;background:var(--vscode-textLink-foreground)}
.state .caveat{margin-left:auto;font-weight:400;opacity:.75}
iframe{border:0;width:100%;height:calc(100vh - 35px);background:#fff}
.message{padding:38px 34px;max-width:58ch}
.message h1{margin:0 0 10px;font-size:16px;font-weight:600}
.message p{margin:0 0 12px;opacity:.85}
.message code{font-family:var(--vscode-editor-font-family);font-size:12px;background:var(--vscode-textCodeBlock-background);padding:1px 5px;border-radius:3px}
.subject{padding:28px 34px}
.subject .line{font-size:19px;font-weight:600;padding:14px 16px;border:1px solid var(--vscode-editorWidget-border);border-radius:7px;background:var(--vscode-editorWidget-background);word-break:break-word}
.subject .meta{margin-top:10px;font-size:12px;opacity:.75}
.subject .over{color:var(--vscode-errorForeground);opacity:1}
pre{margin:0;padding:22px 26px;white-space:pre-wrap;overflow-wrap:anywhere;font-family:var(--vscode-editor-font-family);font-size:12px}`;

function page(bannerLabel: string, caveat: string, body: string): string {
  return `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; frame-src 'self'">
<style>${documentStyle}</style></head><body>
<div class="state"><span class="dot"></span>${escapeText(bannerLabel)}${
    caveat ? `<span class="caveat">${escapeText(caveat)}</span>` : ""
  }</div>
${body}</body></html>`;
}

const liveCaveat = "Live · browser compiler · not publication proof";

export function renderHtmlPreview(label: string, html: string): string {
  return page(label, liveCaveat, `<iframe sandbox srcdoc="${escapeAttribute(html)}"></iframe>`);
}

/**
 * A subject line is a header, not a document. Showing it against the 300-character
 * limit the server enforces at publish time is the point of previewing it here
 * rather than finding out at publication.
 */
export function renderSubjectPreview(html: string): string {
  const text = html
    .replace(/<[^>]*>/gu, "")
    .replace(/&nbsp;/gu, " ")
    .replace(/\s+/gu, " ")
    .trim();
  const limit = 300;
  const over = text.length > limit;
  return page(
    "Subject line",
    liveCaveat,
    `<div class="subject">
  <div class="line">${text ? escapeText(text) : "<em>empty</em>"}</div>
  <p class="meta${over ? " over" : ""}">${text.length} of ${limit} characters${
      over ? " — over the header limit, publication will reject this." : ""
    }</p>
</div>`,
  );
}

export function renderTargetSource(source: string): string {
  return page(
    "Engine template source",
    "Deferred rendering happens on the server",
    `<div class="message"><h1>This template defers to the output engine</h1>
<p>The markup below is the target-language template that will be rendered server-side. The browser
compiler cannot execute it, so there is no visual preview here.</p>
<p>Use <code>Email: Open Verified Preview</code> to render it through the server and see the
result.</p></div>
<pre>${escapeText(source)}</pre>`,
  );
}

export function renderBlocked(errors: number): string {
  return page(
    "Preview unavailable",
    "",
    `<div class="message"><h1>Live preview is blocked by ${errors} error${
      errors === 1 ? "" : "s"
    }</h1>
<p>The document did not compile, so there is nothing to render yet. Every error is listed in the
Problems panel with the line it is on.</p>
<p>The preview updates as soon as the document compiles.</p></div>`,
  );
}

export function renderNothingToRender(headline: string, detail: string): string {
  return page(
    "Nothing to preview",
    "",
    `<div class="message"><h1>${escapeText(headline)}</h1><p>${escapeText(detail)}</p></div>`,
  );
}
