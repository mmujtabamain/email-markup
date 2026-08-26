import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = path.resolve(__dirname, "../..");

test("manifest contributes only the current Email Markup language", () => {
  const manifest = JSON.parse(
    readFileSync(path.join(root, "package.json"), "utf8"),
  );
  assert.deepEqual(manifest.contributes.languages[0].extensions, [".em", ".emt"]);
  assert.match(
    manifest.contributes.languages[0].icon.light,
    /email-markup-light\.svg$/,
  );
  assert.match(
    manifest.contributes.languages[0].icon.dark,
    /email-markup-dark\.svg$/,
  );
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, "limited");
  assert.equal(manifest.main, "./dist/node/extension.js");
  assert.equal(manifest.browser, "./dist/web/extension.js");
  assert.equal(manifest.contributes.commands[0].icon, "$(eye)");
  assert.equal(
    manifest.contributes.menus["editor/title"][0].group,
    "navigation@1",
  );
  assert.equal(
    manifest.contributes.configurationDefaults["[email-markup]"][
      "editor.wordBasedSuggestions"
    ],
    "off",
  );
});

test("extension source enforces trust and a script-free preview", () => {
  const source = readFileSync(path.join(root, "src/extension.ts"), "utf8");
  assert.match(source, /workspace\.isTrusted/);
  assert.match(source, /enableScripts: false/);
  assert.match(source, /localResourceRoots: \[\]/);
  assert.match(source, /typeof result\.html !== "string"/);
  assert.match(source, /onDidChangeTextDocument/);
  assert.match(source, /previewRefreshDelayMs/);
  assert.match(source, /Template not rendered/);
  assert.match(source, /does not execute Django/);
});

test("production bundle has no unresolved language-service module imports", () => {
  const bundle = readFileSync(path.join(root, "dist/node/extension.js"), "utf8");
  assert.doesNotMatch(bundle, /require\(["']\.\/parser\/cssParser["']\)/);
  assert.doesNotMatch(bundle, /require\(["']vscode-languageclient\/node["']\)/);
  const webBundle = readFileSync(path.join(root, "dist/web/extension.js"), "utf8");
  assert.doesNotMatch(webBundle, /node:(path|fs|process|child_process)/);
  assert.doesNotMatch(webBundle, /vscode-languageclient\/node/);
});

test("browser extension derives projects from em.json without host-specific schemes", () => {
  // The project model lives beside the extension so it can be exercised without
  // an editor; the guarantees it has to keep are unchanged.
  const model = readFileSync(path.join(root, "src/web/workspaceModel.ts"), "utf8");
  assert.match(model, /path\.endsWith\("\/em\.json"\)/);
  assert.match(model, /config\.include/);
  assert.match(model, /config\.imports/);
  assert.match(model, /config\?\.shell/);
  assert.match(model, /config\?\.engine/);
  assert.match(model, /\$\{EMAIL_MARKUP_LIB\}/);
  assert.doesNotMatch(model, /scheme:\s*"email-content"/);
  assert.doesNotMatch(model, /"\/components"|"\/shells"|"\/styles"|"\/templates"/);

  const source = readFileSync(path.join(root, "src/web/extension.ts"), "utf8");
  // Selectors carry no scheme at all: a provider pinned to `file` never fires
  // over the virtual file system the hosted editor serves documents from.
  assert.match(
    source,
    /registerCompletionItemProvider\(\s*\{ language: "email-markup" \}/,
  );
  assert.doesNotMatch(source, /scheme:\s*"email-content"/);
  assert.doesNotMatch(source, /scheme:\s*"file"/);
  assert.doesNotMatch(source, /"\/components"|"\/shells"|"\/styles"|"\/templates"/);
});

test("browser compiler uses a classic worker compatible with web extension hosts", () => {
  const client = readFileSync(path.join(root, "src/web/browserClient.ts"), "utf8");
  const worker = readFileSync(
    path.join(root, "../../packages/email-markup-browser/worker/email-markup.worker.mjs"),
    "utf8",
  );
  assert.match(
    client,
    /new Worker\(this\.workerUrl, \{ name: "email-markup-compiler" \}\)/,
  );
  assert.doesNotMatch(client, /type:\s*"module"/);
  // A failed worker is retired and replaced rather than latching the client dead
  // for the rest of the session.
  assert.match(client, /private ensureWorker\(\): Worker/);
  assert.match(client, /worker\.terminate\(\)/);
  assert.match(worker, /import\("\.\/email-markup-browser\.mjs"\)/);
  assert.match(worker, /let requestChain = Promise\.resolve\(\)/);
  assert.match(worker, /modulePromise = undefined/);
  assert.match(worker, /attempt < 2/);
  assert.match(worker, /function errorStack\(error, message\)/);
  assert.match(worker, /errorStack\(error, message\)/);
  assert.doesNotMatch(worker, /^import\s/m);
});

test("every staged language server includes its runtime assets", () => {
  const serverRoot = path.join(root, "server");
  const platforms = readdirSync(serverRoot, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => entry.name);
  for (const platform of platforms) {
    const staged = path.join(serverRoot, platform);
    assert.doesNotThrow(() =>
      readFileSync(path.join(staged, "lib/builtins.em")),
    );
    assert.doesNotThrow(() =>
      readFileSync(path.join(staged, "lib/engines/django.emt")),
    );
  }
});

test("generated grammar records the shared lexical source", () => {
  const lexical = JSON.parse(
    readFileSync(path.join(root, "../../syntax/lexical.json"), "utf8"),
  );
  const grammar = JSON.parse(
    readFileSync(
      path.join(root, "syntaxes/email-markup.tmLanguage.json"),
      "utf8",
    ),
  );
  assert.equal(grammar.metadata.generatedFrom, "syntax/lexical.json");
  assert.equal(grammar.metadata.version, lexical.version);
  const props = JSON.stringify(grammar.repository.props);
  for (const declarationType of [
    ...lexical.propTypes,
    ...lexical.deferredParameterTypes,
  ]) {
    assert.match(props, new RegExp(`\\b${declarationType}\\b`));
  }
  assert.equal(grammar.scopeName, "source.email-markup");
  assert.ok(
    grammar.patterns.some(
      (pattern: { include?: string }) => pattern.include === "text.html.basic",
    ),
  );

  const manifest = JSON.parse(
    readFileSync(path.join(root, "package.json"), "utf8"),
  );
  assert.equal(
    manifest.contributes.grammars[0].embeddedLanguages["text.html.basic"],
    "html",
  );
  assert.equal(
    manifest.contributes.grammars[0].embeddedLanguages["source.css"],
    "css",
  );
  const injection = JSON.parse(
    readFileSync(
      path.join(root, "syntaxes/email-markup.injection.tmLanguage.json"),
      "utf8",
    ),
  );
  assert.match(injection.injectionSelector, /text\.html\.basic/);
  assert.match(injection.injectionSelector, /source\.css/);
  assert.match(
    grammar.repository.invocation.patterns[2].captures[1].name,
    /support\.type\.property-name\.email-markup/,
  );
  assert.match(
    grammar.repository.props.patterns[2].captures[1].name,
    /support\.type\.property-name\.email-markup/,
  );
  assert.match(grammar.repository.deferred.name, /deferred-call/);
  assert.match(grammar.repository.params.name, /block\.params/);
  assert.ok(
    grammar.repository.defineStyle.patterns.some(
      (pattern: { include?: string }) =>
        pattern.include === "source.css#rule-list-innards",
    ),
  );
  assert.ok(
    grammar.repository.defineStyle.patterns.some(
      (pattern: { include?: string }) =>
        pattern.include === "#namedDefinitionArguments",
    ),
  );
  assert.match(
    grammar.repository.namedDefinitionArguments.patterns[0].captures[1].name,
    /support\.type\.property-name\.email-markup/,
  );
  const invocationString = grammar.repository.invocation.patterns.find(
    (pattern: { name?: string }) =>
      pattern.name === "string.quoted.double.email-markup",
  );
  assert.ok(
    invocationString.patterns.some(
      (pattern: { include?: string }) => pattern.include === "#interpolation",
    ),
  );

  const configuration = JSON.parse(
    readFileSync(path.join(root, "language-configuration.json"), "utf8"),
  );
  assert.match(configuration.wordPattern, /A-Za-z0-9_\.-/);
  const wordPattern = new RegExp(configuration.wordPattern, "g");
  assert.deepEqual("@{business.".match(wordPattern), ["business."]);
  assert.deepEqual('style: "quote-'.match(wordPattern), ["style", "quote-"]);
  assert.deepEqual(configuration.brackets, [
    ["(", ")"],
    ["{", "}"],
    ["[", "]"],
  ]);
  assert.match(configuration.folding.markers.start, /Else/);
  assert.equal(
    new RegExp(configuration.folding.markers.start).test("@Else"),
    false,
  );
  assert.ok(configuration.onEnterRules.length > 0);
});

test("the browser extension registers before it reads the project", () => {
  const source = readFileSync(path.join(root, "src/web/extension.ts"), "utf8");

  // Activation used to await the whole project walk before registering
  // anything, so opening `em.json` waited on every file in the repository
  // before the editor that renders it existed.
  const registration = source.indexOf("registerJsonEditors()");
  const load = source.indexOf("await project.load()");
  assert.ok(registration > 0 && load > 0);
  assert.ok(registration < load, "providers must be registered before the project is read");

  // The web extension host worker refuses these and says so, repeatedly.
  assert.doesNotMatch(source, /globalThis\.addEventListener/);
  assert.doesNotMatch(source, /addEventListener\("unhandledrejection"/);
});

test("the browser extension never stats a file to ask whether it exists", () => {
  // Growth Console's provider acquires a file lease inside `stat` for anything
  // writable, so probing a dozen configured paths that way locked a dozen files
  // for an author who was only looking at `em.json`.
  for (const file of ["src/web/jsonEditors.ts", "src/web/extension.ts", "src/web/project.ts"]) {
    const source = readFileSync(path.join(root, file), "utf8");
    assert.doesNotMatch(source, /workspace\.fs\.stat/, file);
  }
});
