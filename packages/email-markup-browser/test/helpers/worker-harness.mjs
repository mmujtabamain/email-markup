/**
 * The worker is a Web Worker module: it reaches for `self`, and it loads the
 * Emscripten module through a relative import. Node has neither, so a
 * behavioural test of the worker needs both supplied — otherwise the only thing
 * left to check is that the file contains the right words, which is what the
 * package test did and why none of the failures below were caught.
 *
 * `loadWorker` gives it a `self` it can talk to and a sibling module it can
 * import, both under test control. The worker file itself is the real one,
 * copied unmodified; nothing here rewrites the code under test.
 */
import { copyFileSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const pkg = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const workerSource = path.join(pkg, "worker", "email-markup.worker.mjs");

/**
 * The worker caches the loaded module in a module-level variable, so a scenario
 * needs its own copy of the module graph. Node keys the ESM cache by URL, and a
 * fresh directory is the only way to get a fresh URL for an import the worker
 * writes itself (`./email-markup-browser.mjs`).
 */
const stubModule = `
const factory = () => globalThis.__EMAIL_MARKUP_STUB__.createModule();
export default factory;
`;

/**
 * A stand-in for the Emscripten module.
 *
 * `heap: false` drops the allocator exports, which is how a build made before
 * the heap path existed looks to the worker. `respond` receives the request
 * string and returns the response string, or throws to simulate a trap.
 */
export function stubModuleFactory({
  respond = () => JSON.stringify({ ok: true }),
  heap = true,
  getExceptionMessage,
  mallocReturns,
} = {}) {
  const calls = { ccall: [], heap: [], malloc: 0, free: 0, requests: [] };
  const module = {
    calls,
    ccall(name, returnType, argumentTypes, args) {
      calls.ccall.push(args[0]);
      calls.requests.push(args[0]);
      return respond(args[0]);
    },
  };
  if (heap) {
    const buffers = new Map();
    let next = 8;
    module._malloc = (bytes) => {
      calls.malloc += 1;
      if (mallocReturns !== undefined) return mallocReturns;
      const pointer = next;
      next += bytes;
      buffers.set(pointer, "");
      return pointer;
    };
    module._free = (pointer) => {
      calls.free += 1;
      buffers.delete(pointer);
    };
    module.lengthBytesUTF8 = (text) => Buffer.byteLength(text, "utf8");
    module.stringToUTF8 = (text, pointer) => {
      buffers.set(pointer, text);
    };
    module.UTF8ToString = (value) => value;
    module._email_markup_browser_request = (pointer) => {
      const request = buffers.get(pointer);
      calls.heap.push(request);
      calls.requests.push(request);
      return respond(request);
    };
  }
  if (getExceptionMessage) module.getExceptionMessage = getExceptionMessage;
  return module;
}

/**
 * Loads the real worker with a controlled environment.
 *
 * `createModule` is called every time the worker (re)loads the compiler, so a
 * test can hand back a different module on the second attempt and observe the
 * retry rather than infer it from a regex.
 */
export async function loadWorker({ createModule } = {}) {
  const directory = mkdtempSync(path.join(os.tmpdir(), "email-markup-worker-"));
  copyFileSync(workerSource, path.join(directory, "email-markup.worker.mjs"));
  writeFileSync(path.join(directory, "email-markup-browser.mjs"), stubModule);

  const messages = [];
  const waiters = [];
  let listener;
  let loads = 0;

  const previousSelf = Object.getOwnPropertyDescriptor(globalThis, "self");
  const previousStub = globalThis.__EMAIL_MARKUP_STUB__;

  globalThis.__EMAIL_MARKUP_STUB__ = {
    createModule: () => {
      loads += 1;
      return createModule ? createModule(loads) : stubModuleFactory();
    },
  };
  const fakeSelf = {
    addEventListener(type, callback) {
      if (type === "message") listener = callback;
    },
    postMessage(message) {
      const waiter = waiters.shift();
      if (waiter) waiter(message);
      else messages.push(message);
    },
  };
  Object.defineProperty(globalThis, "self", {
    value: fakeSelf,
    configurable: true,
    writable: true,
  });

  await import(pathToFileURL(path.join(directory, "email-markup.worker.mjs")).href);

  return {
    /** Deliver a message the way a Web Worker would. */
    post(data) {
      listener({ data });
    },
    /** Resolves with the next message the worker posts back. */
    next() {
      if (messages.length) return Promise.resolve(messages.shift());
      return new Promise((resolve) => waiters.push(resolve));
    },
    /** One request in, one response out — the common case. */
    async send(data) {
      this.post(data);
      return this.next();
    },
    get loads() {
      return loads;
    },
    dispose() {
      if (previousSelf) Object.defineProperty(globalThis, "self", previousSelf);
      else delete globalThis.self;
      globalThis.__EMAIL_MARKUP_STUB__ = previousStub;
      rmSync(directory, { recursive: true, force: true });
    },
  };
}

/** A well-formed envelope, so a test only states the part it is about. */
export function request(overrides = {}) {
  return {
    protocol: "email-markup.browser",
    version: 1,
    id: "test",
    method: "capabilities",
    params: {},
    ...overrides,
  };
}

/** The response shape the compiler returns for a successful request. */
export function successResponse(id = "test", result = {}) {
  return JSON.stringify({
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: "1.2.0",
    id,
    ok: true,
    result,
  });
}

export function errorResponse(id = "test", code = "invalid_request", message = "bad") {
  return JSON.stringify({
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: "1.2.0",
    id,
    ok: false,
    error: { code, message },
  });
}
