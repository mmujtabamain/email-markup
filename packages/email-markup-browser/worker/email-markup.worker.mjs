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

function failure(id, code, message) {
  return {
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: null,
    id: id ?? null,
    ok: false,
    error: { code, message },
  };
}

function failureMessage(error) {
  if (error instanceof Error) return error.message;
  if (typeof error === "number") {
    return `Email Markup WebAssembly threw exception ${error}.`;
  }
  return `Email Markup worker failed: ${String(error)}`;
}

function isRecoverableCompilerFailure(error) {
  return error instanceof RecoverableCompilerError ||
    error instanceof SyntaxError ||
    typeof error === "number" ||
    (typeof WebAssembly !== "undefined" && error instanceof WebAssembly.RuntimeError);
}

async function callCompiler(request) {
  for (let attempt = 0; attempt < 2; attempt += 1) {
    try {
      const module = await loadModule();
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
      if (!isRecoverableCompilerFailure(error) || attempt === 1) throw error;
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
    self.postMessage(
      failure(
        id,
        "worker_failure",
        failureMessage(error),
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
