/**
 * A rejected request must come back as the protocol's error envelope.
 *
 * This is the contract the editor is built on: `handle_request` catches
 * everything it can and answers `{ ok: false, error: { code, message } }`, so a
 * mistake in a request produces something an author can be shown. When it
 * escapes WebAssembly instead, the host receives a pointer — the editor reports
 * "threw exception 4321496" — and the worker discards a compiler that had
 * nothing wrong with it and reloads it, for every keystroke that repeats the
 * mistake.
 *
 * The packaged artifact in `dist/` cannot satisfy this yet: it was linked with
 * `-sDISABLE_EXCEPTION_CATCHING=0` but compiled without `-fexceptions`, so
 * Emscripten compiled every `catch` clause in the compiler away — all nineteen
 * in the core library and both nets in the browser entry point. The build now
 * requests exception support at compile time (see the root `CMakeLists.txt`);
 * these tests turn green with the next `npm run verify:wasm`, and the marker
 * below should be deleted when they do.
 */
import assert from "node:assert/strict";
import test from "node:test";

import { attempt, callRaw, loadCompiler, readDistFile } from "./helpers/compiler.mjs";

const STALE_ARTIFACT = {
  todo:
    "dist/email-markup-browser.wasm predates the compile-time -fexceptions fix; " +
    "rebuild with `npm run verify:wasm` and remove this marker",
};

const builtins = readDistFile("lib/builtins.em");

/** Sends a request and insists on a rejection rather than a trap. */
async function rejection(method, params, id = "e") {
  const outcome = await attempt(method, params, id);
  assert.equal(
    outcome.trapped,
    false,
    `the compiler threw out of WebAssembly instead of answering: ${outcome.error?.message}`,
  );
  assert.equal(outcome.response.ok, false, "this request should have been rejected");
  assert.equal(outcome.response.protocol, "email-markup.browser");
  assert.equal(outcome.response.version, 1);
  assert.ok(outcome.response.error.message.length > 0);
  return outcome.response;
}

/** The same, for a request that is not even valid JSON. */
async function rejectionOfText(text) {
  const module = await loadCompiler();
  let raw;
  try {
    raw = callRaw(module, text);
  } catch (error) {
    assert.fail(`the compiler threw out of WebAssembly instead of answering: ${error}`);
  }
  const response = JSON.parse(raw);
  assert.equal(response.ok, false);
  return response;
}

test("a request that is not JSON is reported as bad JSON", STALE_ARTIFACT, async () => {
  const response = await rejectionOfText("{not json");
  assert.equal(response.error.code, "invalid_json");
  assert.equal(response.id, null);
});

test("a request for another protocol is refused", STALE_ARTIFACT, async () => {
  const module = await loadCompiler();
  const response = JSON.parse(
    callRaw(
      module,
      JSON.stringify({ protocol: "other", version: 1, id: "e", method: "capabilities", params: {} }),
    ),
  );
  assert.equal(response.ok, false);
  assert.equal(response.error.code, "invalid_request");
  assert.match(response.error.message, /unsupported request protocol/);
});

test("a request for a future protocol version is refused", STALE_ARTIFACT, async () => {
  const module = await loadCompiler();
  const response = JSON.parse(
    callRaw(
      module,
      JSON.stringify({
        protocol: "email-markup.browser",
        version: 2,
        id: "e",
        method: "capabilities",
        params: {},
      }),
    ),
  );
  assert.equal(response.ok, false);
  assert.match(response.error.message, /protocol version/);
});

test("an envelope with an unknown field is refused", STALE_ARTIFACT, async () => {
  const module = await loadCompiler();
  const response = JSON.parse(
    callRaw(
      module,
      JSON.stringify({
        protocol: "email-markup.browser",
        version: 1,
        id: "e",
        method: "capabilities",
        params: {},
        extra: 1,
      }),
    ),
  );
  assert.equal(response.ok, false);
  assert.match(response.error.message, /unknown fields/);
});

test("an id that is neither a string nor an integer is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("capabilities", {}, { object: true });
  assert.match(response.error.message, /id must be a string or integer/);
});

test("an unknown method is reported as an unknown method", STALE_ARTIFACT, async () => {
  // A host on a newer protocol asking for something this build does not serve
  // needs to be told that, not that its `entry_path` is missing.
  const response = await rejection("explode", {});
  assert.equal(response.error.code, "invalid_request");
  assert.match(response.error.message, /unsupported method explode/);
  assert.equal(response.id, "e", "a rejection still belongs to the request that caused it");
});

test("capabilities takes no parameters", STALE_ARTIFACT, async () => {
  const response = await rejection("capabilities", { verbose: true });
  assert.match(response.error.message, /capabilities params must be empty/);
});

test("a relative entry path is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", { entry_path: "message.em", source: "x" });
  assert.match(response.error.message, /absolute virtual path/);
});

test("a source file that is not Email Markup is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", { entry_path: "/message.txt", source: "x" });
  assert.match(response.error.message, /unsupported source extension/);
});

test("an unknown workspace field is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    include_paths: ["/lib"],
  });
  assert.match(response.error.message, /unknown workspace fields/);
});

test("compile data that is not an object is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    data: 5,
  });
  assert.match(response.error.message, /data must be an object/);
});

test("an unknown output context is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    output_context: "pdf",
  });
  assert.match(response.error.message, /html or subject/);
});

test("a virtual file that repeats the entry path is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    files: [{ path: "/message.em", source: "y" }],
  });
  assert.match(response.error.message, /duplicates entry_path/);
});

test("two virtual files at the same path are refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    files: [
      { path: "/shared.em", source: "y" },
      { path: "/shared.em", source: "z" },
    ],
  });
  assert.match(response.error.message, /duplicate virtual Email Markup path/);
});

test("a shell that is also listed among the files is refused, not fatal", STALE_ARTIFACT, async () => {
  // A host that lists every project file and *also* names the shell hits this,
  // which is an easy mistake to make and must stay diagnosable.
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    files: [{ path: "/shells/default.em", source: "@Slot(default);" }],
    shell: { path: "/shells/default.em", source: "@Slot(default);" },
  });
  assert.match(response.error.message, /duplicate virtual Email Markup path/);
});

test("more than 256 virtual files is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x",
    files: Array.from({ length: 257 }, (_, index) => ({
      path: `/file-${index}.em`,
      source: "y",
    })),
  });
  assert.match(response.error.message, /at most 256/);
});

test("a source over the 1 MiB limit is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("analyze", {
    entry_path: "/message.em",
    source: "x".repeat(1024 * 1024 + 1),
  });
  assert.match(response.error.message, /1 MiB/);
});

test("a position-taking method without a position is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("complete", { entry_path: "/message.em", source: "@" });
  assert.match(response.error.message, /complete requires position/);
});

test("a negative position is refused rather than wrapping around", STALE_ARTIFACT, async () => {
  const response = await rejection("complete", {
    entry_path: "/message.em",
    source: "@",
    position: { line: -1, character: 0 },
  });
  assert.match(response.error.message, /unsigned UTF-16/);
});

test("format without a source is refused", STALE_ARTIFACT, async () => {
  const response = await rejection("format", { path: "/message.em" });
  assert.match(response.error.message, /format requires path and source/);
});

test(
  "a component that forwards its slot into another component compiles",
  STALE_ARTIFACT,
  async () => {
    // `components/notice.em` in the Growth Console content repository is exactly
    // this shape: a component whose template wraps a builtin and passes its own
    // default slot through. Expanding it used to recurse without bound and take
    // the worker down with a stack overflow — so opening that file, or using
    // @Notice in any template, killed the editor's compiler.
    const source = [
      '@DefineComponent(name: "Notice")',
      "  @Slots",
      "    default: required",
      "  @/Slots",
      "  @Template",
      "    @Panel",
      "      @Slot(default);",
      "    @/Panel",
      "  @/Template",
      "@/DefineComponent",
      "",
      "@Notice",
      "  <p>Body</p>",
      "@/Notice",
    ].join("\n");
    const outcome = await attempt(
      "analyze",
      {
        entry_path: "/templates/message.em",
        source,
        files: [{ path: "/lib/builtins.em", source: builtins }],
        include_directories: ["/lib"],
        imports: ["/lib/builtins.em"],
        data: {},
      },
      "slot-forwarding",
    );
    assert.equal(outcome.trapped, false, "expanding a forwarded slot must terminate");
    assert.equal(outcome.response.result.success, true);
    assert.match(outcome.response.result.preview.html, /Body/);
  },
);
