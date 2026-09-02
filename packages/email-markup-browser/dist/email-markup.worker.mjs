const maximumRequestBytes = 1024 * 1024;
const encoder = new TextEncoder();

let modulePromise;
let requestChain = Promise.resolve();

class RecoverableCompilerError extends Error {}

/** The request cannot be delivered to this build at all; retrying changes nothing. */
class RequestTooLargeError extends Error {}

function createModule() {
  return import("./email-markup-browser.mjs")
    .then(({ default: createEmailMarkup }) => createEmailMarkup());
}

/**
 * A load that failed is not an answer worth keeping. Caching the rejected
 * promise meant one bad fetch of the `.wasm` — a cold cache, a dropped
 * connection, a proxy hiccup — retired the compiler for the lifetime of the
 * worker: every later request inherited the same rejection and the editor
 * stayed silent until the page was reloaded. Forgetting it costs one retry and
 * lets the next keystroke recover.
 */
function loadModule() {
  modulePromise ??= createModule().catch((error) => {
    resetModule();
    throw error;
  });
  return modulePromise;
}

function resetModule() {
  modulePromise = undefined;
}

function failure(id, code, message, stack) {
  return {
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: null,
    id: id ?? null,
    ok: false,
    error: { code, message, ...(stack ? { stack } : {}) },
  };
}

/**
 * An exception that escapes the compiler reaches JavaScript as a bare pointer
 * into WebAssembly memory. Reported as-is it is a number and nothing else —
 * "threw exception 225832" names no cause, no file and no line, which is what
 * the editor used to hand the author.
 *
 * Emscripten can translate the pointer back into the exception's type and
 * message when the module is built with the exception-handling helpers. Modules
 * built before that flag was added still exist, so the number is kept as a last
 * resort and the message says what it actually means.
 */
function decodeWebAssemblyException(module, pointer) {
  try {
    const decoded = module?.getExceptionMessage?.(pointer);
    if (Array.isArray(decoded)) {
      const [type, message] = decoded;
      if (message) return `${type ?? "exception"}: ${message}`;
      if (type) return String(type);
    }
  } catch {
    // Falling through to the untranslated form is better than replacing one
    // unhelpful error with a different one.
  }
  return (
    "the Email Markup compiler aborted without reporting a reason " +
    `(WebAssembly exception ${pointer}). A project that includes itself — a shell ` +
    "configured as its own shell, or a cyclic @Include — is the usual cause."
  );
}

function failureMessage(error, module) {
  if (error instanceof Error) return error.message;
  if (typeof error === "number") return decodeWebAssemblyException(module, error);
  return `Email Markup worker failed: ${String(error)}`;
}

function errorStack(error, message) {
  if (error instanceof Error && error.stack) return error.stack;
  return new Error(message).stack;
}

function isRecoverableCompilerFailure(error) {
  if (error instanceof RequestTooLargeError) return false;
  return error instanceof RecoverableCompilerError ||
    error instanceof SyntaxError ||
    typeof error === "number" ||
    (typeof WebAssembly !== "undefined" && error instanceof WebAssembly.RuntimeError);
}

/*
 * How the request crosses into WebAssembly.
 *
 * `ccall` copies a string argument onto the *stack*. Emscripten's default stack
 * is 64 KiB, so the real limit on a request was never the protocol's 1 MiB — it
 * was the stack, and a project large enough to cross it faulted with "memory
 * access out of bounds", which reached the author as an unexplained WebAssembly
 * exception with a pointer for a message.
 *
 * Given the allocator, the request goes on the heap instead and the limit is
 * the protocol's again. Builds without it fall back to the stack path, and are
 * told what their real limit is rather than being allowed to fault.
 */
const stackRequestLimit = 32 * 1024;

function callThroughHeap(module, request) {
  const length = module.lengthBytesUTF8(request) + 1;
  const pointer = module._malloc(length);
  if (!pointer) throw new RecoverableCompilerError("could not allocate the request buffer");
  try {
    module.stringToUTF8(request, pointer, length);
    return module.UTF8ToString(module._email_markup_browser_request(pointer));
  } finally {
    module._free(pointer);
  }
}

function callCompilerOnce(module, request) {
  const heapCapable =
    typeof module._malloc === "function" &&
    typeof module._free === "function" &&
    typeof module.stringToUTF8 === "function" &&
    typeof module.lengthBytesUTF8 === "function" &&
    typeof module.UTF8ToString === "function";
  if (heapCapable) return callThroughHeap(module, request);

  // Stack path. Refusing an oversize request is the whole point: letting it
  // through is a fault, and a fault here cannot be attributed to anything.
  if (encoder.encode(request).byteLength > stackRequestLimit) {
    throw new RequestTooLargeError(
      `this Email Markup compiler build passes requests on a ${stackRequestLimit / 1024} KiB ` +
        "stack, and this project is larger than that. Rebuild the browser compiler to raise " +
        "the limit, or analyze fewer files at once.",
    );
  }
  return module.ccall("email_markup_browser_request", "string", ["string"], [request]);
}

async function callCompiler(request) {
  for (let attempt = 0; attempt < 2; attempt += 1) {
    let module;
    try {
      module = await loadModule();
      const response = JSON.parse(callCompilerOnce(module, request));
      if (!response?.ok && response.error?.code === "internal_error") {
        throw new RecoverableCompilerError(
          response.error.message ?? "Email Markup browser compiler hit an internal error.",
        );
      }
      return response;
    } catch (error) {
      if (!isRecoverableCompilerFailure(error) || attempt === 1) {
        throw typeof error === "number"
          ? new Error(failureMessage(error, module))
          : error;
      }
      resetModule();
    }
  }
  throw new Error("Email Markup browser compiler did not produce a response.");
}

async function handleMessage(data) {
  const id = data?.id ?? null;
  try {
    const request = JSON.stringify(data);
    if (encoder.encode(request).byteLength > maximumRequestBytes) {
      self.postMessage(
        failure(id, "invalid_request", "request exceeds the 1 MiB protocol limit"),
      );
      return;
    }
    self.postMessage(await callCompiler(request));
  } catch (error) {
    const message = failureMessage(
      error,
      await Promise.resolve(modulePromise).catch(() => undefined),
    );
    self.postMessage(
      failure(
        id,
        // A request this build cannot carry is a property of the request, not a
        // failure of the worker, and the caller can act on the difference.
        error instanceof RequestTooLargeError ? "request_too_large" : "worker_failure",
        message,
        errorStack(error, message),
      ),
    );
  }
}

self.addEventListener("message", (event) => {
  const data = event.data;
  requestChain = requestChain.then(
    () => handleMessage(data),
    () => handleMessage(data),
  );
});
