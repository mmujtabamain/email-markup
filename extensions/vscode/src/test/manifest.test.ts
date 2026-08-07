import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = path.resolve(__dirname, "../..");

test("manifest contributes only the current ELL language", () => {
  const manifest = JSON.parse(readFileSync(path.join(root, "package.json"), "utf8"));
  assert.deepEqual(manifest.contributes.languages[0].extensions, [".ell"]);
  assert.match(manifest.contributes.languages[0].icon.light, /ell-light\.svg$/);
  assert.match(manifest.contributes.languages[0].icon.dark, /ell-dark\.svg$/);
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, "limited");
  assert.equal(manifest.main, "./dist/extension.js");
  assert.equal(manifest.contributes.commands[0].icon, "$(eye)");
  assert.equal(manifest.contributes.menus["editor/title"][0].group, "navigation@1");
});

test("extension source enforces trust and a script-free preview", () => {
  const source = readFileSync(path.join(root, "src/extension.ts"), "utf8");
  assert.match(source, /workspace\.isTrusted/);
  assert.match(source, /enableScripts: false/);
  assert.match(source, /localResourceRoots: \[\]/);
  assert.match(source, /typeof result\.html !== "string"/);
  assert.match(source, /onDidChangeTextDocument/);
  assert.match(source, /previewRefreshDelayMs/);
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
    assert.doesNotThrow(() => readFileSync(path.join(staged, "lib/builtins.ell")));
    assert.doesNotThrow(() => readFileSync(path.join(staged, "brand/example/brand.ell")));
    assert.doesNotThrow(() => readFileSync(path.join(staged, "brand/example/styles.ell")));
    assert.doesNotThrow(() => readFileSync(path.join(staged, "brand/example/shell.ell")));
  }
});

test("generated grammar records the shared lexical source", () => {
  const grammar = JSON.parse(
    readFileSync(path.join(root, "syntaxes/ell.tmLanguage.json"), "utf8"),
  );
  assert.equal(grammar.metadata.generatedFrom, "syntax/lexical.json");
  assert.equal(grammar.scopeName, "source.ell");
  assert.ok(grammar.patterns.some((pattern: { include?: string }) => pattern.include === "text.html.basic"));

  const manifest = JSON.parse(readFileSync(path.join(root, "package.json"), "utf8"));
  assert.equal(manifest.contributes.grammars[0].embeddedLanguages["text.html.basic"], "html");
  assert.equal(manifest.contributes.grammars[0].embeddedLanguages["source.css"], "css");
  const injection = JSON.parse(
    readFileSync(path.join(root, "syntaxes/ell.injection.tmLanguage.json"), "utf8"),
  );
  assert.match(injection.injectionSelector, /text\.html\.basic/);
  assert.match(injection.injectionSelector, /source\.css/);
});
