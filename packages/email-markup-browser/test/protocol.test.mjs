/**
 * Conformance tests for the packaged WebAssembly compiler.
 *
 * These run against `dist/` — the bytes Growth Console serves — and cover every
 * method the protocol advertises. The previous artifact test called two of the
 * six and asserted nothing about the other four, so completion, hover,
 * signature help and the formatter shipped unverified.
 */
import assert from "node:assert/strict";
import test from "node:test";

import { loadCompiler, readDistFile, readFixture, send } from "./helpers/compiler.mjs";

const builtins = readDistFile("lib/builtins.em");
const django = readDistFile("lib/engines/django.emt");
const contextSchema = JSON.parse(readFixture("context.schema.json"));

/** A workspace shaped like a Growth Console content checkout. */
function workspace(overrides = {}) {
  return {
    entry_path: "/templates/check/message.em",
    source: readFixture("local.em"),
    files: [{ path: "/lib/builtins.em", source: builtins }],
    include_directories: ["/lib"],
    imports: ["/lib/builtins.em"],
    data: { business: { name: "Northstar", rating: 4.8 } },
    ...overrides,
  };
}

test("the protocol version export matches the packaged protocol", async () => {
  const compiler = await loadCompiler();
  assert.equal(
    compiler.ccall("email_markup_browser_protocol_version", "number", [], []),
    1,
  );
});

test("capabilities reports the trust boundary and the whole method set", async () => {
  const response = await send("capabilities", {}, "cap");
  assert.equal(response.ok, true);
  assert.equal(response.protocol, "email-markup.browser");
  assert.equal(response.version, 1);
  assert.match(response.compiler_version, /^\d+\.\d+\.\d+$/);
  assert.deepEqual(response.result.methods, [
    "capabilities",
    "analyze",
    "format",
    "complete",
    "hover",
    "signature",
  ]);
  assert.equal(response.result.authoritative, false);
  assert.equal(response.result.executes_target, false);
  assert.equal(response.result.network_access, false);
  assert.equal(response.result.position_encoding, "utf-16");
});

test("capabilities publishes the limits the host has to respect", async () => {
  const { result } = await send("capabilities", {}, "cap");
  assert.deepEqual(result.limits, {
    request_bytes: 1048576,
    source_bytes: 1048576,
    workspace_source_bytes: 2097152,
    files: 256,
    diagnostics: 100,
    output_bytes: 2097152,
  });
});

test("the compiler version is the package version", async () => {
  const { result } = await send("capabilities", {}, "cap");
  const manifest = JSON.parse(readDistFile("manifest.json"));
  assert.equal(result.compiler_version, manifest.compilerVersion);
});

test("analyze compiles a document to final HTML with its data substituted", async () => {
  const { ok, result } = await send("analyze", workspace(), "analyze");
  assert.equal(ok, true);
  assert.equal(result.success, true);
  assert.equal(result.authoritative, false, "browser output is never authoritative");
  assert.equal(result.output_kind, "final-html");
  assert.equal(result.preview.kind, "final-html");
  assert.equal(result.preview.rendered, true);
  assert.equal(result.preview.executes_target, false);
  assert.match(result.preview.html, /Hello Northstar/);
  assert.match(result.preview.html, /You are rated 4\.8\./);
  assert.deepEqual(result.diagnostics, []);
});

test("analyze reports the files the document actually reached", async () => {
  const { result } = await send("analyze", workspace(), "analyze");
  assert.deepEqual(result.dependencies, ["/lib/builtins.em", "/templates/check/message.em"]);
});

test("analyze lists the symbols the document declares", async () => {
  const { result } = await send("analyze", workspace(), "analyze");
  const signoff = result.symbols.find((symbol) => symbol.name === "Signoff");
  assert.ok(signoff, "a component declared in the document should be a symbol");
  assert.equal(signoff.kind, "component");
  assert.equal(signoff.range.start.line, 0);
});

test("analyze reports a bad document as a diagnostic, not as a failed request", async () => {
  const { ok, result } = await send(
    "analyze",
    workspace({ source: "@Missing @/Missing" }),
    "bad",
  );
  assert.equal(ok, true, "an invalid document is still a valid request");
  assert.equal(result.success, false);
  assert.equal(result.preview, null);
  assert.ok(result.diagnostics.length > 0);
  const [diagnostic] = result.diagnostics;
  assert.equal(diagnostic.severity, "error");
  assert.match(diagnostic.code, /^EM\d{4}$/);
  assert.ok(diagnostic.range.start, "a diagnostic must be placeable in the editor");
  assert.equal(diagnostic.path, "/templates/check/message.em");
});

test("a diagnostic's range is UTF-16, so it lands where Monaco puts the cursor", async () => {
  // The emoji is one code point and two UTF-16 units, which is the whole point:
  // a byte offset or a code-point offset would put the squiggle in the wrong
  // column from here on.
  const { result } = await send(
    "analyze",
    workspace({ source: "<p>🙂</p>\n@Missing @/Missing" }),
    "utf16",
  );
  const diagnostic = result.diagnostics.find((entry) => entry.range);
  assert.equal(diagnostic.range.start.line, 1);
  assert.equal(diagnostic.range.start.character, 0);
});

test("analyze fills an empty preview from the context schema's examples", async () => {
  const { result } = await send(
    "analyze",
    workspace({ source: "<p>@{business.name}</p>", data: {}, context_schema: contextSchema }),
    "schema",
  );
  assert.equal(result.success, true);
  assert.match(result.preview.html, /Amina Coffee Roasters/);
});

test("analyze renders the deferred engine as target source, never executed", async () => {
  const { result } = await send(
    "analyze",
    workspace({
      source: "<p>Hi @[business.name]</p>",
      engine: { path: "/lib/engines/django.emt", source: django },
      data: {},
      context_schema: contextSchema,
    }),
    "engine",
  );
  assert.equal(result.success, true);
  assert.equal(result.output_kind, "engine-template");
  assert.equal(result.preview.executes_target, false);
  assert.equal(result.target.name, "django");
});

test("analyze of an engine definition describes it without compiling a document", async () => {
  const { result } = await send(
    "analyze",
    {
      entry_path: "/lib/engines/django.emt",
      source: django,
      files: [],
      include_directories: [],
      imports: [],
      data: {},
    },
    "emt",
  );
  assert.equal(result.success, true);
  assert.equal(result.output_kind, "engine-definition");
  assert.equal(result.preview.kind, "target-source");
  assert.equal(result.preview.rendered, false);
  assert.equal(result.preview.executes_target, false);
  const names = result.symbols.map((symbol) => symbol.name);
  assert.deepEqual(names, ["BareTemplate", "For", "If"]);
});

test("analyze composes the shell around the document", async () => {
  const { result } = await send(
    "analyze",
    workspace({
      source: "<p>Body text</p>",
      files: [
        { path: "/lib/builtins.em", source: builtins },
        { path: "/styles/project.em", source: readFixture("project.em") },
      ],
      include_directories: ["/lib", "/styles", "/shells"],
      imports: ["/lib/builtins.em", "/styles/project.em"],
      shell: { path: "/shells/default.em", source: readFixture("shell.em") },
      engine: { path: "/lib/engines/django.emt", source: django },
      data: {},
      context_schema: contextSchema,
    }),
    "shell",
  );
  assert.equal(result.success, true);
  assert.ok(
    result.dependencies.includes("/shells/default.em"),
    "the shell is part of what the document depends on",
  );
});

test("analyze produces a subject line as text rather than markup", async () => {
  const { result } = await send(
    "analyze",
    workspace({ source: "Hi @{business.name}", output_context: "subject" }),
    "subject",
  );
  assert.equal(result.success, true);
  assert.equal(result.preview.html, "Hi Northstar");
});

test("analyze is deterministic — the same request twice gives the same answer", async () => {
  const first = await send("analyze", workspace(), "same");
  const second = await send("analyze", workspace(), "same");
  assert.deepEqual(first, second);
});

test("format normalises a document and says whether it changed it", async () => {
  const messy = await send("format", { path: "/t/m.em", source: "@Heading    Hi   @/Heading" }, "f");
  assert.equal(messy.ok, true);
  assert.equal(messy.result.changed, true);
  assert.equal(messy.result.text, "@Heading Hi @/Heading\n");

  const already = await send("format", { path: "/t/m.em", source: messy.result.text }, "f2");
  assert.equal(already.result.changed, false, "formatting is idempotent");
  assert.equal(already.result.text, messy.result.text);
});

test("format accepts an engine definition as well as a document", async () => {
  const response = await send("format", { path: "/e/django.emt", source: django }, "fmt-emt");
  assert.equal(response.ok, true);
  assert.equal(typeof response.result.text, "string");
});

test("complete offers the language's keywords after an @", async () => {
  const { result } = await send(
    "complete",
    workspace({ source: "@", position: { line: 0, character: 1 } }),
    "c",
  );
  assert.equal(result.is_incomplete, false);
  const labels = result.items.map((item) => item.label);
  for (const keyword of ["@If", "@For", "@Include", "@DefineComponent"]) {
    assert.ok(labels.includes(keyword), `expected ${keyword} among the completions`);
  }
  const [first] = result.items;
  assert.deepEqual(first.replace, {
    start: { line: 0, character: 0 },
    end: { line: 0, character: 1 },
  });
});

test("complete offers the components in scope, including imported ones", async () => {
  // Typing `@` at the end of the real document, so both the component it
  // declares and the ones it imports are in scope.
  const source = `${readFixture("local.em")}@`;
  const line = source.split("\n").length - 1;
  const { result } = await send(
    "complete",
    workspace({ source, position: { line, character: 1 } }),
    "c",
  );
  const components = result.items.filter((item) => item.kind === "component");
  const labels = components.map((item) => item.label);
  assert.ok(labels.includes("@Signoff"), "a component declared in the document");
  assert.ok(labels.includes("@Heading"), "a component imported from the standard library");
  const heading = components.find((item) => item.label === "@Heading");
  assert.match(heading.insert_text, /^@Heading/);
});

test("complete offers data paths inside an interpolation", async () => {
  const { result } = await send(
    "complete",
    workspace({ source: "<p>@{bus", position: { line: 0, character: 8 } }),
    "c",
  );
  const labels = result.items.map((item) => item.label);
  assert.deepEqual(labels, ["business", "business.name", "business.rating"]);
  assert.equal(result.items[0].kind, "module", "an object is a module, a leaf is a value");
  assert.equal(result.items[1].kind, "value");
});

test("complete prefers the context schema over sample data, with its documentation", async () => {
  const { result } = await send(
    "complete",
    workspace({
      source: "<p>@{",
      position: { line: 0, character: 5 },
      data: {},
      context_schema: contextSchema,
    }),
    "c",
  );
  const business = result.items.find((item) => item.label === "business");
  assert.equal(business.detail, "object · required");
  assert.equal(business.documentation, "The business receiving the email.");
  const name = result.items.find((item) => item.label === "business.name");
  assert.equal(name.detail, "string · required");
});

test("complete offers a component's remaining props inside its argument list", async () => {
  const { result } = await send(
    "complete",
    workspace({ source: "@Button(", position: { line: 0, character: 8 } }),
    "c",
  );
  const labels = result.items.map((item) => item.label);
  assert.ok(labels.includes("url"), "@Button takes a url prop");
  assert.equal(result.items[0].kind, "property");
  assert.match(result.items[0].insert_text, /: $/, "a prop completion leaves the cursor after the colon");
});

test("complete offers prop types inside a @Props block", async () => {
  const source = '@DefineComponent(name: "Card")\n  @Props\n    label: \n  @/Props\n';
  const { result } = await send(
    "complete",
    workspace({ source, position: { line: 2, character: 11 } }),
    "c",
  );
  const labels = result.items.map((item) => item.label);
  assert.deepEqual(labels, ["string", "int", "decimal", "number", "bool", "name", "url", "email", "color"]);
  assert.equal(result.items[0].kind, "type");
});

test("hover explains a component and the props it declares", async () => {
  const { result } = await send(
    "hover",
    workspace({ source: "@Button(url: \"https://example.invalid\") Go @/Button", position: { line: 0, character: 4 } }),
    "h",
  );
  assert.match(result.markdown, /\*\*@Button\*\*/);
  assert.match(result.markdown, /url/);
});

test("hover explains a schema field where the author's cursor is", async () => {
  const { result } = await send(
    "hover",
    workspace({
      source: "<p>@{business.name}</p>",
      position: { line: 0, character: 12 },
      data: {},
      context_schema: contextSchema,
    }),
    "h",
  );
  assert.match(result.markdown, /\*\*business\.name\*\*/);
  assert.match(result.markdown, /`string` · required/);
  assert.match(result.markdown, /Amina Coffee Roasters/);
});

test("hover over ordinary prose answers with nothing rather than guessing", async () => {
  const { ok, result } = await send(
    "hover",
    workspace({ source: "<p>plain words</p>", position: { line: 0, character: 6 } }),
    "h",
  );
  assert.equal(ok, true);
  assert.equal(result, null);
});

test("signature describes the component being called and the active argument", async () => {
  const { result } = await send(
    "signature",
    workspace({ source: "@Button(url: ", position: { line: 0, character: 13 } }),
    "s",
  );
  assert.equal(result.label, "@Button(url: url)");
  assert.deepEqual(result.parameters, [{ label: "url: url" }]);
  assert.equal(result.active_parameter, 0);
});

test("signature counts commas to follow the cursor between arguments", async () => {
  const { result } = await send(
    "signature",
    workspace({
      source: '@Image(src: "https://example.invalid/a.png", ',
      position: { line: 0, character: 44 },
    }),
    "s",
  );
  assert.equal(result.active_parameter, 1);
});

test("signature outside a call answers with nothing", async () => {
  const { ok, result } = await send(
    "signature",
    workspace({ source: "<p>text</p>", position: { line: 0, character: 5 } }),
    "s",
  );
  assert.equal(ok, true);
  assert.equal(result, null);
});

test("every response carries the caller's id back, whatever its type", async () => {
  for (const id of ["a-string", 0, 42, -1]) {
    const response = await send("capabilities", {}, id);
    assert.equal(response.id, id);
  }
});
