/**
 * The browser compiler against content shaped like Growth Console's.
 *
 * The other suites take the protocol apart method by method. This one walks the
 * path an author actually takes — open a template that sits inside a shell,
 * with tokens, an imported component, a deferred engine and a context schema;
 * ask for completions; hover something; format; compile a subject — because
 * every failure found so far only appeared once the pieces were combined.
 */
import assert from "node:assert/strict";
import test from "node:test";

import { readDistFile, readFixture, send } from "./helpers/compiler.mjs";

const builtins = readDistFile("lib/builtins.em");
const django = readDistFile("lib/engines/django.emt");
const contextSchema = JSON.parse(readFixture("context.schema.json"));

/** The whole content checkout, as a host would send it. */
function checkout(overrides = {}) {
  return {
    entry_path: "/templates/five-point-website-check/body.em",
    source: readFixture("message.em"),
    files: [
      { path: "/lib/builtins.em", source: builtins },
      { path: "/components/notice.em", source: readFixture("notice.em") },
      { path: "/styles/project.em", source: readFixture("project.em") },
    ],
    include_directories: ["/lib", "/components", "/styles", "/shells"],
    imports: ["/lib/builtins.em", "/components/notice.em", "/styles/project.em"],
    shell: { path: "/shells/default.em", source: readFixture("shell.em") },
    engine: { path: "/lib/engines/django.emt", source: django },
    data: {},
    context_schema: contextSchema,
    ...overrides,
  };
}

/** The same checkout without the component whose expansion used to recurse. */
function plainCheckout(overrides = {}) {
  return checkout({
    source: [
      "<p>Hi @[business.name] team,</p>",
      "@If[business.rating]",
      "  <p>You are rated @[business.rating].</p>",
      "@Slot(else)",
      "  <p>Your customers clearly rate you.</p>",
      "@/Slot",
      "@/If",
      '<p><a href="@[message.unsubscribe_url]">Unsubscribe</a></p>',
    ].join("\n"),
    ...overrides,
  });
}

test("a deferred template compiles against its shell, tokens and engine", async () => {
  const { ok, result } = await send("analyze", plainCheckout(), "content");
  assert.equal(ok, true);
  assert.equal(result.success, true, JSON.stringify(result.diagnostics));
  assert.equal(result.output_kind, "engine-template");
  assert.equal(result.target.name, "django");
  assert.equal(result.preview.executes_target, false, "the browser never runs Django");
});

test("the shell, the component and the token file are all reported as dependencies", async () => {
  const { result } = await send("analyze", plainCheckout(), "content");
  for (const dependency of ["/shells/default.em", "/styles/project.em", "/lib/builtins.em"]) {
    assert.ok(
      result.dependencies.includes(dependency),
      `${dependency} should be a dependency of the compiled template`,
    );
  }
});

test("a token the shell uses resolves rather than being left in the output", async () => {
  const { result } = await send("analyze", plainCheckout(), "content");
  const output = result.preview.html ?? result.preview.source ?? "";
  assert.doesNotMatch(output, /@\{token\./, "an unresolved token would ship to the recipient");
});

test("the subject compiles as its own document", async () => {
  const { result } = await send(
    "analyze",
    plainCheckout({
      entry_path: "/templates/five-point-website-check/subject.em",
      source: readFixture("subject.em"),
      output_context: "subject",
      shell: undefined,
    }),
    "subject",
  );
  assert.equal(result.success, true, JSON.stringify(result.diagnostics));
});

test("a mistyped component is reported where it was typed", async () => {
  const { result } = await send(
    "analyze",
    plainCheckout({ source: "@Notic\n  <p>Body</p>\n@/Notic" }),
    "unknown-component",
  );
  assert.equal(result.success, false);
  const [diagnostic] = result.diagnostics;
  assert.equal(diagnostic.code, "EM0720");
  assert.equal(diagnostic.range.start.line, 0);
});

test("an unclosed component is reported, not silently accepted", async () => {
  const { result } = await send(
    "analyze",
    plainCheckout({ source: "@Heading A faster website" }),
    "unclosed",
  );
  assert.equal(result.success, false);
  assert.equal(result.diagnostics[0].code, "EM0205");
});

test(
  "a mistyped merge field is a diagnostic, not the end of the compiler",
  async () => {
    // Typing a field name wrong is the most ordinary mistake there is. In the
    // shipped artifact it escapes WebAssembly as `std::runtime_error: expression
    // error`, so the editor loses its compiler mid-sentence instead of putting a
    // squiggle under the word.
    const { result } = await send(
      "analyze",
      {
        entry_path: "/templates/body.em",
        source: "<p>Hi @{business.nickname}</p>",
        files: [{ path: "/lib/builtins.em", source: builtins }],
        include_directories: ["/lib"],
        imports: ["/lib/builtins.em"],
        data: { business: { name: "Northstar" } },
      },
      "unknown-path",
    );
    assert.equal(result.success, false);
    assert.equal(result.diagnostics[0].code, "EM1200");
    assert.equal(result.diagnostics[0].range.start.line, 0);
  },
);

test(
  "a component missing a required prop is a diagnostic, not the end of the compiler",
  async () => {
    const { result } = await send(
      "analyze",
      {
        entry_path: "/templates/body.em",
        source: "@Button Go @/Button",
        files: [{ path: "/lib/builtins.em", source: builtins }],
        include_directories: ["/lib"],
        imports: ["/lib/builtins.em"],
        data: {},
      },
      "missing-prop",
    );
    assert.equal(result.success, false);
    assert.equal(result.diagnostics[0].code, "EM0723");
    assert.match(result.diagnostics[0].message, /requires prop/);
  },
);

test("completion inside a deferred value offers the schema's fields", async () => {
  const { result } = await send(
    "complete",
    plainCheckout({ source: "<p>@[business.", position: { line: 0, character: 14 } }),
    "complete",
  );
  const labels = result.items.map((item) => item.label);
  assert.ok(labels.includes("business.name"));
  assert.ok(labels.includes("message.unsubscribe_url"));
});

test("hovering a merge field explains it from the schema, not from sample data", async () => {
  const { result } = await send(
    "hover",
    plainCheckout({ source: "<p>@[business.rating]</p>", position: { line: 0, character: 16 } }),
    "hover",
  );
  assert.match(result.markdown, /\*\*business\.rating\*\*/);
  assert.match(result.markdown, /nullable/);
});

test("formatting a template leaves its deferred syntax alone", async () => {
  const source = readFixture("message.em");
  const { result } = await send("format", { path: "/t/body.em", source }, "format");
  assert.match(result.text, /@\[business\.name\]/);
  assert.match(result.text, /@If\[business\.rating\]/);
});

test("analysing the same checkout twice gives byte-identical answers", async () => {
  const first = await send("analyze", plainCheckout(), "same");
  const second = await send("analyze", plainCheckout(), "same");
  assert.deepEqual(first, second);
});

test(
  "a template that uses the shared Notice component compiles",
  async () => {
    const { result } = await send("analyze", checkout(), "notice");
    assert.equal(result.success, true, JSON.stringify(result.diagnostics));
    assert.match(result.preview.source ?? result.preview.html, /free homepage mockup/);
  },
);

test("opening the Notice component itself previews it", async () => {
  // Analysing a definition-only file renders a preview of each component it
  // declares, so this is what the editor does the moment the file is opened.
  const { result } = await send(
    "analyze",
    {
      entry_path: "/components/notice.em",
      source: readFixture("notice.em"),
      files: [{ path: "/lib/builtins.em", source: builtins }],
      include_directories: ["/lib"],
      imports: ["/lib/builtins.em"],
      data: {},
    },
    "notice-file",
  );
  assert.equal(result.success, true, JSON.stringify(result.diagnostics));
  assert.equal(result.preview.kind, "final-html");
});
