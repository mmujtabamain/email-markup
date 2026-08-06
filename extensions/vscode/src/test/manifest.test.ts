import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = path.resolve(__dirname, "../..");

test("manifest contributes only the current ELL language", () => {
  const manifest = JSON.parse(readFileSync(path.join(root, "package.json"), "utf8"));
  assert.deepEqual(manifest.contributes.languages[0].extensions, [".ell"]);
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, "limited");
  assert.equal(manifest.main, "./dist/extension.js");
});

test("extension source enforces trust and a script-free preview", () => {
  const source = readFileSync(path.join(root, "src/extension.ts"), "utf8");
  assert.match(source, /workspace\.isTrusted/);
  assert.match(source, /enableScripts: false/);
  assert.match(source, /localResourceRoots: \[\]/);
});

test("generated grammar records the shared lexical source", () => {
  const grammar = JSON.parse(
    readFileSync(path.join(root, "syntaxes/ell.tmLanguage.json"), "utf8"),
  );
  assert.equal(grammar.metadata.generatedFrom, "syntax/lexical.json");
  assert.equal(grammar.scopeName, "source.ell");
});
