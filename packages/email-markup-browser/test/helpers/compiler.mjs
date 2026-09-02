/**
 * Loads the packaged WebAssembly compiler — the exact artifact the editor ships
 * — and calls it the way the worker does.
 *
 * Every test in this directory runs against `dist/`, not against a fresh build,
 * because `dist/` is what Growth Console serves. A green suite here means the
 * bytes an author downloads behave.
 */
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const pkg = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
export const dist = path.join(pkg, "dist");
export const root = path.resolve(pkg, "../..");

let compiler;

export async function loadCompiler() {
  if (compiler) return compiler;
  const wasmBinary = readFileSync(path.join(dist, "email-markup-browser.wasm"));
  const { default: createEmailMarkup } = await import(
    pathToFileURL(path.join(dist, "email-markup-browser.mjs")).href
  );
  compiler = await createEmailMarkup({ wasmBinary });
  return compiler;
}

/** The raw C boundary, exactly as the worker's stack path calls it. */
export function callRaw(module, request) {
  return module.ccall("email_markup_browser_request", "string", ["string"], [request]);
}

export function envelope(method, params, id = "test") {
  return { protocol: "email-markup.browser", version: 1, id, method, params };
}

/**
 * Sends a request and parses the response.
 *
 * A C++ exception that escapes the module arrives here as a bare number, so it
 * is turned into an Error naming the exception — otherwise a failing assertion
 * would read `4321496` and say nothing about what went wrong.
 */
export async function send(method, params, id = "test") {
  const module = await loadCompiler();
  try {
    return JSON.parse(callRaw(module, JSON.stringify(envelope(method, params, id))));
  } catch (error) {
    throw asError(module, error);
  }
}

/**
 * Sends a request that is expected to be *rejected*, and reports how.
 *
 * `{ trapped: true }` means the compiler threw out of WebAssembly instead of
 * answering, which is never a valid protocol outcome: the caller receives a
 * dead module rather than a diagnosable error.
 */
export async function attempt(method, params, id = "test") {
  const module = await loadCompiler();
  let text;
  try {
    text = callRaw(module, JSON.stringify(envelope(method, params, id)));
  } catch (error) {
    return { trapped: true, error: asError(module, error) };
  }
  return { trapped: false, response: JSON.parse(text) };
}

function asError(module, error) {
  if (typeof error !== "number") return error instanceof Error ? error : new Error(String(error));
  try {
    const decoded = module.getExceptionMessage?.(error);
    if (Array.isArray(decoded)) {
      const failure = new Error(decoded.filter(Boolean).join(": "));
      failure.wasmException = true;
      return failure;
    }
  } catch {
    // The pointer is all there is; say so rather than replacing it with noise.
  }
  const failure = new Error(`WebAssembly exception ${error}`);
  failure.wasmException = true;
  return failure;
}

export function readFixture(name) {
  return readFileSync(path.join(pkg, "test", "fixtures", name), "utf8");
}

export function readDistFile(name) {
  return readFileSync(path.join(dist, name), "utf8");
}
