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
  assert.equal(manifest.main, "./dist/extension.js");
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
  const bundle = readFileSync(path.join(root, "dist/extension.js"), "utf8");
  assert.doesNotMatch(bundle, /require\(["']\.\/parser\/cssParser["']\)/);
  assert.doesNotMatch(bundle, /require\(["']vscode-languageclient\/node["']\)/);
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
