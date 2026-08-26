import assert from "node:assert/strict";
import test from "node:test";

import {
  buildWorkspace,
  libraryRoot,
  maximumVirtualSources,
  outputContextFor,
  type ProjectSnapshot,
} from "../web/workspaceModel";

const config = JSON.stringify({
  include: ["${EMAIL_MARKUP_LIB}", "components", "shells", "styles"],
  imports: [
    "${EMAIL_MARKUP_LIB}/builtins.em",
    "components/notice.em",
    "styles/project.em",
  ],
  context_schema: "schemas/email-context.em.schema.json",
  shell: "shells/default.em",
  engine: "${EMAIL_MARKUP_LIB}/engines/django.emt",
  out: ".build",
});

function snapshot(): ProjectSnapshot {
  return {
    files: new Map([
      [`${libraryRoot}/builtins.em`, "@// builtins"],
      [`${libraryRoot}/engines/django.emt`, "@// django engine"],
      ["/components/notice.em", '@DefineComponent(name: "Notice")\n@/DefineComponent'],
      ["/styles/project.em", '@DefineToken(name: "accent", value: "#8E51D0");'],
      ["/shells/default.em", "<html><body>@Slot(default);</body></html>"],
      ["/templates/welcome/body.em", "<p>Hello</p>"],
      ["/templates/welcome/subject.em", "A note for you"],
    ]),
    json: new Map([
      ["/em.json", config],
      [
        "/schemas/email-context.em.schema.json",
        JSON.stringify({ fields: { business: { type: "object" } } }),
      ],
    ]),
  };
}

test("an ordinary template is wrapped in the project shell and engine", () => {
  const { workspace, roles } = buildWorkspace(
    snapshot(),
    "/templates/welcome/body.em",
    "<p>Hello</p>",
  );
  assert.deepEqual(roles, { isShell: false, isEngine: false, isImported: false });
  assert.equal(workspace.shell?.path, "/shells/default.em");
  assert.equal(workspace.engine?.path, `${libraryRoot}/engines/django.emt`);
  assert.equal(workspace.output_context, "html");
  const paths = workspace.files.map((file) => file.path);
  assert.ok(!paths.includes("/templates/welcome/body.em"), "the entry is passed separately");
  assert.ok(!paths.includes("/shells/default.em"), "the shell is passed separately");
  assert.ok(paths.includes("/components/notice.em"));
});

test("a shell opened as the entry is never handed to itself", () => {
  const { workspace, roles } = buildWorkspace(
    snapshot(),
    "/shells/default.em",
    "<html><body>@Slot(default);</body></html>",
  );
  assert.equal(roles.isShell, true);
  assert.equal(
    workspace.shell,
    undefined,
    "a shell asked to embed itself exhausts the compiler stack",
  );
  assert.equal(workspace.entry_path, "/shells/default.em");
  assert.ok(!workspace.files.some((file) => file.path === "/shells/default.em"));
});

test("an engine opened as the entry is never handed to itself", () => {
  const { workspace, roles } = buildWorkspace(
    snapshot(),
    `${libraryRoot}/engines/django.emt`,
    "@// django engine",
  );
  assert.equal(roles.isEngine, true);
  assert.equal(workspace.engine, undefined);
});

test("the entry is dropped from imports when the project imports it", () => {
  const { workspace, roles } = buildWorkspace(
    snapshot(),
    "/components/notice.em",
    '@DefineComponent(name: "Notice")\n@/DefineComponent',
  );
  assert.equal(roles.isImported, true);
  assert.ok(
    !workspace.imports.includes("/components/notice.em"),
    "the compiler must never be told to import a file it was not given",
  );
  assert.ok(!workspace.files.some((file) => file.path === "/components/notice.em"));
  // Everything else the project imports is still in scope.
  assert.ok(workspace.imports.includes("/styles/project.em"));
});

test("every import is a file the compiler was actually handed", () => {
  for (const entry of [
    "/components/notice.em",
    "/styles/project.em",
    "/shells/default.em",
    "/templates/welcome/body.em",
  ]) {
    const { workspace } = buildWorkspace(snapshot(), entry, "x");
    const supplied = new Set([
      ...workspace.files.map((file) => file.path),
      workspace.shell?.path,
      workspace.engine?.path,
    ]);
    for (const imported of workspace.imports) {
      assert.ok(supplied.has(imported), `${entry}: import ${imported} was not supplied`);
    }
  }
});

test("subject templates compile as a subject line, not a document", () => {
  assert.equal(outputContextFor("/templates/welcome/subject.em"), "subject");
  assert.equal(outputContextFor("/templates/welcome/body.em"), "html");
  assert.equal(outputContextFor("/subject.em"), "subject");
  assert.equal(outputContextFor("/templates/resubject.em"), "html");
  const { workspace } = buildWorkspace(
    snapshot(),
    "/templates/welcome/subject.em",
    "A note for you",
  );
  assert.equal(workspace.output_context, "subject");
});

test("extra imports bring a file into scope for a synthetic preview entry", () => {
  const { workspace } = buildWorkspace(
    snapshot(),
    "/components/__email-markup-preview__.em",
    "@Notice\n  body\n@/Notice",
    { extraImports: ["/components/notice.em"] },
  );
  assert.ok(workspace.imports.includes("/components/notice.em"));
  assert.ok(workspace.files.some((file) => file.path === "/components/notice.em"));
});

test("a caller-supplied shell overrides the configured one", () => {
  const { workspace } = buildWorkspace(snapshot(), "/__email-markup-preview__.em", "<p>x</p>", {
    shellPath: "/shells/default.em",
  });
  assert.equal(workspace.shell?.path, "/shells/default.em");
});

test("files beyond the protocol cap are reported rather than dropped silently", () => {
  const base = snapshot();
  const files = new Map(base.files);
  for (let index = 0; index < maximumVirtualSources + 10; index += 1) {
    files.set(`/filler/file-${index}.em`, "@// filler");
  }
  const { workspace, dropped } = buildWorkspace(
    { files, json: base.json },
    "/templates/welcome/body.em",
    "<p>Hello</p>",
  );
  assert.equal(workspace.files.length, maximumVirtualSources);
  assert.ok(dropped.length > 0, "truncation has to be visible to the caller");
  // What the entry depends on is kept; filler is what goes.
  assert.ok(workspace.files.some((file) => file.path === "/components/notice.em"));
  assert.ok(dropped.every((path) => path.startsWith("/filler/")));
});

test("a project with no em.json still gets the packaged library", () => {
  const base = snapshot();
  const { workspace } = buildWorkspace(
    { files: base.files, json: new Map() },
    "/templates/welcome/body.em",
    "<p>Hello</p>",
  );
  assert.deepEqual(workspace.imports, [`${libraryRoot}/builtins.em`]);
  assert.deepEqual(workspace.include_directories, [libraryRoot]);
  assert.equal(workspace.shell, undefined);
});

test("an unparseable em.json is reported and does not take the project down", () => {
  const base = snapshot();
  const warnings: string[] = [];
  const { workspace } = buildWorkspace(
    { files: base.files, json: new Map([["/em.json", "{ not json"]]) },
    "/templates/welcome/body.em",
    "<p>Hello</p>",
    {},
    (message) => warnings.push(message),
  );
  assert.equal(warnings.length, 1);
  assert.match(warnings[0], /em\.json/);
  assert.ok(workspace.files.length > 0);
});

test("the context schema travels with the workspace", () => {
  const { workspace } = buildWorkspace(
    snapshot(),
    "/templates/welcome/body.em",
    "<p>Hello</p>",
  );
  assert.ok(workspace.context_schema);
  assert.ok("business" in (workspace.context_schema?.fields as object));
});
