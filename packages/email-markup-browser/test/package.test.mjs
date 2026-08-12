import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const pkg = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const root = path.resolve(pkg, "../..");

test("worker uses the versioned protocol without target execution", () => {
  const worker = readFileSync(path.join(pkg, "worker/email-markup.worker.mjs"), "utf8");
  assert.match(worker, /email-markup\.browser/);
  assert.match(worker, /email_markup_browser_request/);
  assert.match(worker, /maximumRequestBytes = 1024 \* 1024/);
  assert.match(worker, /let requestChain = Promise\.resolve\(\)/);
  assert.match(worker, /modulePromise = undefined/);
  assert.match(worker, /attempt < 2/);
  assert.match(worker, /internal_error/);
  assert.doesNotMatch(worker, /eval\(|new Function|document\.|fetch\(/);
});

test("browser package exposes worker, types, and protocol schema", () => {
  const manifest = JSON.parse(readFileSync(path.join(pkg, "package.json"), "utf8"));
  const version = readFileSync(path.join(root, "VERSION"), "utf8").trim();
  assert.equal(manifest.version, version);
  assert.equal(manifest.exports["."].default, "./dist/email-markup.worker.mjs");
  assert.equal(manifest.exports["."].types, "./dist/index.d.ts");
  assert.equal(manifest.exports["./protocol"], "./dist/browser-protocol-v1.schema.json");
  assert.equal(manifest.exports["./manifest"], "./dist/manifest.json");
  assert.equal(manifest.emailMarkup.browserProtocol, 1);
  assert.equal(manifest.emailMarkup.toolchain.emscriptenVersion, "4.0.23");
  assert.match(manifest.emailMarkup.toolchain.emsdkCommit, /^[0-9a-f]{40}$/);
  assert.match(manifest.emailMarkup.toolchain.emscriptenCommit, /^[0-9a-f]{40}$/);
  assert.match(manifest.emailMarkup.toolchain.releasesHash, /^[0-9a-f]{40}$/);
  const schema = JSON.parse(
    readFileSync(path.join(root, "schema/browser-protocol-v1.schema.json"), "utf8"),
  );
  assert.equal(schema.$id, "https://email-markup.dev/schema/browser-protocol-v1.schema.json");
});

test("WASM build is browser-only and exports the stable C boundary", () => {
  const cmake = readFileSync(path.join(pkg, "CMakeLists.txt"), "utf8");
  const build = readFileSync(path.join(pkg, "scripts/build-wasm.mjs"), "utf8");
  assert.match(cmake, /email_markup_browser_request/);
  assert.match(cmake, /ENVIRONMENT=worker/);
  assert.match(cmake, /FILESYSTEM=0/);
  assert.match(build, /EMAIL_MARKUP_BROWSER_ONLY=ON/);
  assert.match(build, /email-markup-browser-wasm/);
});
