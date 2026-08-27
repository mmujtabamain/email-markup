import assert from "node:assert/strict";
import test from "node:test";

import type { AnalyzeResult } from "../web/browserClient";
import { readDefinitions } from "../web/definitions";
import {
  galleryEntry,
  planPreview,
  renderBlocked,
  renderSubjectPreview,
  tokenEntry,
} from "../web/preview";
import type { EntryRoles } from "../web/workspaceModel";

const noRoles: EntryRoles = { isShell: false, isEngine: false, isImported: false };

function analysis(overrides: Partial<AnalyzeResult> = {}): AnalyzeResult {
  return {
    success: true,
    authoritative: false,
    output_kind: "final-html",
    diagnostics: [],
    dependencies: [],
    symbols: [],
    preview: null,
    ...overrides,
  };
}

test("errors are reported as errors, with a count", () => {
  const plan = planPreview(
    "/templates/welcome/body.em",
    analysis({
      diagnostics: [
        { code: "e1", severity: "error", message: "bad" },
        { code: "e2", severity: "error", message: "worse" },
        { code: "w1", severity: "warning", message: "hmm" },
      ],
    }),
    readDefinitions("<p>x</p>"),
    noRoles,
    "html",
  );
  assert.deepEqual(plan, { kind: "blocked", errors: 2 });
  assert.match(renderBlocked(2), /blocked by 2 errors/);
  assert.match(renderBlocked(1), /blocked by 1 error(?!s)/);
});

test("a component library previews as a gallery, not as a failure", () => {
  const source = `@DefineComponent(name: "Notice")
  @Slots
    default: required
  @/Slots
  @Template
    <section>@Slot(default);</section>
  @/Template
@/DefineComponent
`;
  const plan = planPreview(
    "/components/notice.em",
    analysis(),
    readDefinitions(source),
    { ...noRoles, isImported: true },
    "html",
  );
  assert.equal(plan.kind, "gallery");
  assert.equal(plan.kind === "gallery" && plan.components[0].name, "Notice");
});

test("a token sheet previews as swatches", () => {
  const plan = planPreview(
    "/styles/project.em",
    analysis(),
    readDefinitions('@DefineToken(name: "accent", value: "#8E51D0");'),
    noRoles,
    "html",
  );
  assert.equal(plan.kind, "tokens");
});

test("a shell previews with placeholder content inside it", () => {
  const plan = planPreview(
    "/shells/default.em",
    analysis(),
    readDefinitions("<html><body>@Slot(default);</body></html>"),
    { ...noRoles, isShell: true },
    "html",
  );
  assert.equal(plan.kind, "shell");
});

test("a document that renders is previewed as itself", () => {
  const plan = planPreview(
    "/templates/welcome/body.em",
    analysis({ preview: { kind: "final-html", html: "<p>Hello</p>" } }),
    readDefinitions("<p>Hello</p>"),
    noRoles,
    "html",
  );
  assert.deepEqual(plan, { kind: "document", outputContext: "html" });
});

test("an engine definition says so instead of claiming an error", () => {
  const plan = planPreview(
    "/lib/engines/django.emt",
    analysis({ output_kind: "engine-definition" }),
    readDefinitions("@// engine"),
    { ...noRoles, isEngine: true },
    "html",
  );
  assert.equal(plan.kind, "none");
  assert.match(plan.kind === "none" ? plan.headline : "", /engine/i);
});

test("the gallery instantiates each component with placeholder props and slots", () => {
  const source = `@DefineComponent(name: "Metric")
  @Props
    label: string
    accent: color = "#2563eb"
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <div>@Slot(default);</div>
  @/Template
@/DefineComponent
`;
  const entry = galleryEntry(readDefinitions(source).components);
  assert.match(entry, /@Metric\(label: "Sample label", accent: "#2563eb"\)/);
  assert.match(entry, /@\/Metric/);
  assert.doesNotMatch(entry, /@Include/, "the file is supplied through imports, not an include");
});

test("a component with no slots is instantiated as a statement", () => {
  const source = `@DefineComponent(name: "Rule")
  @Template
    <hr>
  @/Template
@/DefineComponent
`;
  assert.match(galleryEntry(readDefinitions(source).components), /@Rule;/);
});

test("the swatch sheet reads every token through the compiler", () => {
  const tokens = readDefinitions(
    '@DefineToken(name: "accent", value: "#8E51D0");',
  ).tokens;
  const entry = tokenEntry(tokens);
  assert.match(entry, /@\{token\.accent\}/);
  assert.doesNotMatch(entry, /#8E51D0/, "the value shown must be the compiler's, not the parser's");
});

test("a subject preview counts against the header limit the server enforces", () => {
  const within = renderSubjectPreview("<p>A short subject</p>");
  assert.match(within, /A short subject/);
  assert.match(within, /15 of 300 characters/);
  assert.doesNotMatch(within, /over the header limit/);

  const over = renderSubjectPreview(`<p>${"x".repeat(301)}</p>`);
  assert.match(over, /301 of 300 characters/);
  assert.match(over, /over the header limit/);
});
