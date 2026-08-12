const maximumRequestBytes = 1024 * 1024;
const encoder = new TextEncoder();

const modulePromise = import("./email-markup-browser.mjs")
  .then(({ default: createEmailMarkup }) => createEmailMarkup());

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
    return `Email Markup WebAssembly threw exception ${error}. The packaged compiler may be out of date.`;
  }
  return `Email Markup worker failed: ${String(error)}`;
}

self.addEventListener("message", async (event) => {
  const id = event.data?.id ?? null;
  try {
    const request = JSON.stringify(event.data);
    if (encoder.encode(request).byteLength > maximumRequestBytes) {
      self.postMessage(
        failure(id, "invalid_request", "request exceeds the 1 MiB protocol limit"),
      );
      return;
    }
    const module = await modulePromise;
    const response = module.ccall(
      "email_markup_browser_request",
      "string",
      ["string"],
      [request],
    );
    self.postMessage(JSON.parse(response));
  } catch (error) {
    self.postMessage(
      failure(
        id,
        "worker_failure",
        failureMessage(error),
      ),
    );
  }
});
