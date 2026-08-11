import createEmailMarkup from "./email-markup-browser.mjs";

const maximumRequestBytes = 1024 * 1024;
const encoder = new TextEncoder();

const modulePromise = createEmailMarkup({
  locateFile(file) {
    return new URL(file, import.meta.url).href;
  },
});

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
        error instanceof Error ? error.message : "Email Markup worker failed",
      ),
    );
  }
});
