import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";

const pkg = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dist = path.join(pkg, "dist");

test("packaged WASM is loadable and serves the stable protocol", async () => {
  const wasm = readFileSync(path.join(dist, "email-markup-browser.wasm"));
  const moduleUrl = pathToFileURL(path.join(dist, "email-markup-browser.mjs"));
  const { default: createEmailMarkup } = await import(moduleUrl.href);
  const compiler = await createEmailMarkup({ wasmBinary: wasm });

  assert.equal(
    compiler.ccall("email_markup_browser_protocol_version", "number", [], []),
    1,
  );
  const request = JSON.stringify({
    protocol: "email-markup.browser",
    version: 1,
    id: "artifact-test",
    method: "capabilities",
    params: {},
  });
  const response = JSON.parse(
    compiler.ccall(
      "email_markup_browser_request",
      "string",
      ["string"],
      [request],
    ),
  );
  assert.equal(response.ok, true);
  assert.equal(response.id, "artifact-test");
  assert.equal(response.result.authoritative, false);
  assert.equal(response.result.executes_target, false);
  assert.equal(response.result.network_access, false);
  assert.deepEqual(response.result.methods, [
    "capabilities",
    "analyze",
    "format",
    "complete",
    "hover",
    "signature",
  ]);

  const analysisRequest = JSON.stringify({
    protocol: "email-markup.browser",
    version: 1,
    id: "preview-test",
    method: "analyze",
    params: {
      entry_path: "/project/message.em",
      source: "<p>Hello @{business.name}</p>",
      files: [],
      include_directories: [],
      imports: [],
      data: { business: { name: "Acme" } },
    },
  });
  const analysis = JSON.parse(
    compiler.ccall(
      "email_markup_browser_request",
      "string",
      ["string"],
      [analysisRequest],
    ),
  );
  assert.equal(analysis.ok, true);
  assert.equal(analysis.result.success, true);
  assert.equal(analysis.result.authoritative, false);
  assert.equal(analysis.result.preview.kind, "final-html");
  assert.equal(analysis.result.preview.executes_target, false);
  assert.match(analysis.result.preview.html, /Hello Acme/);
});

test("artifact manifest authenticates the packaged worker assets", () => {
  const manifest = JSON.parse(readFileSync(path.join(dist, "manifest.json"), "utf8"));
  assert.equal(manifest.schema, "email-markup.browser-artifacts");
  assert.equal(manifest.version, 1);
  assert.equal(manifest.browserProtocol, 1);
  assert.equal(manifest.toolchain.emscriptenVersion, "4.0.23");

  for (const [asset, expected] of Object.entries(manifest.assets)) {
    const contents = readFileSync(path.join(dist, asset));
    assert.equal(contents.byteLength, expected.bytes, asset);
    assert.equal(
      createHash("sha256").update(contents).digest("hex"),
      expected.sha256,
      asset,
    );
  }
});

test("WASM imports stay inside the Emscripten worker sandbox", () => {
  const wasm = readFileSync(path.join(dist, "email-markup-browser.wasm"));
  const module = new WebAssembly.Module(wasm);
  const imports = WebAssembly.Module.imports(module);
  assert.ok(imports.length > 0);
  assert.deepEqual(new Set(imports.map((entry) => entry.module)), new Set(["a"]));
  assert.equal(imports.some((entry) => entry.module.startsWith("wasi")), false);
});
