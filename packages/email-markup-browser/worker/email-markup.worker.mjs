const maximumRequestBytes = 1024 * 1024;
const encoder = new TextEncoder();

let modulePromise;
let requestChain = Promise.resolve();

class RecoverableCompilerError extends Error {}

function createModule() {
  return import("./email-markup-browser.mjs")
    .then(({ default: createEmailMarkup }) => createEmailMarkup());
}

function loadModule() {
  modulePromise ??= createModule();
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
  return error instanceof RecoverableCompilerError ||
    error instanceof SyntaxError ||
    typeof error === "number" ||
    (typeof WebAssembly !== "undefined" && error instanceof WebAssembly.RuntimeError);
}

async function callCompiler(request) {
  for (let attempt = 0; attempt < 2; attempt += 1) {
    let module;
    try {
      module = await loadModule();
      const response = JSON.parse(
        module.ccall(
          "email_markup_browser_request",
          "string",
          ["string"],
          [request],
        ),
      );
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
        "worker_failure",
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
