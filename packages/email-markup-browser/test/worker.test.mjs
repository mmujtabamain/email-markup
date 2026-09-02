/**
 * Behavioural tests for the Web Worker that hosts the browser compiler.
 *
 * The worker is the only part of the browser surface an author's keystroke
 * passes through on every request, and until now it was checked by matching
 * regular expressions against its own source. That cannot distinguish "the file
 * mentions a retry" from "a retry happens", which is the difference between the
 * editor recovering from a trapped compiler and the editor going quiet.
 */
import assert from "node:assert/strict";
import test from "node:test";

import {
  errorResponse,
  loadWorker,
  request,
  stubModuleFactory,
  successResponse,
} from "./helpers/worker-harness.mjs";

/** Every test needs the worker torn down, including the ones that fail. */
async function withWorker(options, body) {
  const worker = await loadWorker(options);
  try {
    await body(worker);
  } finally {
    worker.dispose();
  }
}

test("a request is handed to the compiler and its answer is posted back unchanged", async () => {
  await withWorker(
    { createModule: () => stubModuleFactory({ respond: () => successResponse("a", { ok: 1 }) }) },
    async (worker) => {
      const response = await worker.send(request({ id: "a" }));
      assert.deepEqual(response, {
        protocol: "email-markup.browser",
        version: 1,
        compiler_version: "1.2.0",
        id: "a",
        ok: true,
        result: { ok: 1 },
      });
    },
  );
});

test("the request the compiler receives is the message, serialised once", async () => {
  let seen;
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: (text) => {
            seen = text;
            return successResponse("analyze-id");
          },
        }),
    },
    async (worker) => {
      const message = request({
        id: "analyze-id",
        method: "analyze",
        params: { entry_path: "/message.em", source: "<p>Hi</p>" },
      });
      await worker.send(message);
      assert.deepEqual(JSON.parse(seen), message);
    },
  );
});

test("the request crosses into WebAssembly on the heap when the build exports the allocator", async () => {
  let module;
  await withWorker(
    {
      createModule: () => {
        module = stubModuleFactory({ respond: () => successResponse() });
        return module;
      },
    },
    async (worker) => {
      await worker.send(request());
      assert.equal(module.calls.heap.length, 1, "the heap entry point should be used");
      assert.equal(module.calls.ccall.length, 0, "ccall puts the request on the stack");
      assert.equal(module.calls.malloc, 1);
      assert.equal(module.calls.free, 1, "the request buffer must be released");
    },
  );
});

test("the request buffer is released even when the compiler traps", async () => {
  const modules = [];
  await withWorker(
    {
      createModule: () => {
        const module = stubModuleFactory({
          respond: () => {
            throw 4321496;
          },
        });
        modules.push(module);
        return module;
      },
    },
    async (worker) => {
      await worker.send(request());
      assert.equal(modules.length, 2, "a trap should be retried on a fresh module");
      for (const module of modules) {
        assert.equal(module.calls.malloc, 1);
        assert.equal(module.calls.free, 1, "a trapped request must not leak its buffer");
      }
    },
  );
});

test("a build without the allocator falls back to the stack entry point", async () => {
  let module;
  await withWorker(
    {
      createModule: () => {
        module = stubModuleFactory({ heap: false, respond: () => successResponse() });
        return module;
      },
    },
    async (worker) => {
      const response = await worker.send(request());
      assert.equal(response.ok, true);
      assert.equal(module.calls.ccall.length, 1);
    },
  );
});

test("a stack-only build refuses an oversize request instead of faulting on it", async () => {
  let module;
  await withWorker(
    {
      createModule: () => {
        module = stubModuleFactory({ heap: false, respond: () => successResponse() });
        return module;
      },
    },
    async (worker) => {
      const response = await worker.send(
        request({ id: 7, method: "analyze", params: { source: "x".repeat(64 * 1024) } }),
      );
      assert.equal(response.ok, false);
      assert.equal(response.error.code, "request_too_large");
      assert.match(response.error.message, /32 KiB/);
      assert.equal(module.calls.ccall.length, 0, "the request must not reach the stack");
    },
  );
});

test("a request this build cannot carry is not retried", async () => {
  await withWorker(
    { createModule: () => stubModuleFactory({ heap: false }) },
    async (worker) => {
      await worker.send(
        request({ method: "analyze", params: { source: "x".repeat(64 * 1024) } }),
      );
      assert.equal(worker.loads, 1, "reloading the compiler cannot make the request smaller");
    },
  );
});

test("a request beyond the protocol limit is refused before the compiler is loaded", async () => {
  await withWorker({}, async (worker) => {
    const response = await worker.send(
      request({ id: 3, method: "analyze", params: { source: "x".repeat(1024 * 1024 + 1) } }),
    );
    assert.equal(response.ok, false);
    assert.equal(response.id, 3);
    assert.equal(response.error.code, "invalid_request");
    assert.match(response.error.message, /1 MiB/);
    assert.equal(worker.loads, 0, "an unsendable request should not start the compiler");
  });
});

test("the protocol limit is measured in UTF-8 bytes, not characters", async () => {
  await withWorker({}, async (worker) => {
    // Four bytes per character, so 300k characters is 1.2 MiB on the wire while
    // `String.length` still reads as comfortably under the limit.
    const response = await worker.send(
      request({ method: "analyze", params: { source: "𝄞".repeat(300_000) } }),
    );
    assert.equal(response.ok, false);
    assert.equal(response.error.code, "invalid_request");
    assert.equal(worker.loads, 0);
  });
});

test("a trapped compiler is discarded and the request retried on a fresh one", async () => {
  await withWorker(
    {
      createModule: (attempt) =>
        stubModuleFactory({
          respond: () => {
            if (attempt === 1) throw 4321496;
            return successResponse("retry", { recovered: true });
          },
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "retry" }));
      assert.equal(response.ok, true);
      assert.deepEqual(response.result, { recovered: true });
      assert.equal(worker.loads, 2, "the trapped module must not be reused");
    },
  );
});

test("a trap that survives the retry is reported with the exception's type and message", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw 4321496;
          },
          getExceptionMessage: () => [
            "std::invalid_argument",
            "entry_path has an unsupported source extension",
          ],
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "trap" }));
      assert.equal(response.ok, false);
      assert.equal(response.id, "trap");
      assert.equal(response.error.code, "worker_failure");
      assert.equal(
        response.error.message,
        "std::invalid_argument: entry_path has an unsupported source extension",
      );
      assert.ok(response.error.stack, "a failure needs somewhere to have come from");
    },
  );
});

test("an undecodable trap says what the number means rather than printing it alone", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw 4321496;
          },
        }),
    },
    async (worker) => {
      const response = await worker.send(request());
      assert.match(response.error.message, /WebAssembly exception 4321496/);
      assert.match(response.error.message, /includes itself/);
    },
  );
});

test("a decoder that throws still leaves an explained failure", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw 4321496;
          },
          getExceptionMessage: () => {
            throw new Error("the module was already torn down");
          },
        }),
    },
    async (worker) => {
      const response = await worker.send(request());
      assert.equal(response.error.code, "worker_failure");
      assert.match(response.error.message, /WebAssembly exception 4321496/);
    },
  );
});

test("a response that is not JSON is retried and then reported", async () => {
  await withWorker(
    {
      createModule: (attempt) =>
        stubModuleFactory({
          respond: () => (attempt === 1 ? "<not json>" : successResponse("json")),
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "json" }));
      assert.equal(response.ok, true);
      assert.equal(worker.loads, 2);
    },
  );
});

test("an internal compiler error is retried on a fresh module", async () => {
  await withWorker(
    {
      createModule: (attempt) =>
        stubModuleFactory({
          respond: () =>
            attempt === 1
              ? errorResponse("internal", "internal_error", "unexpected internal exception")
              : successResponse("internal", { recovered: true }),
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "internal" }));
      assert.equal(response.ok, true);
      assert.deepEqual(response.result, { recovered: true });
      assert.equal(worker.loads, 2);
    },
  );
});

test("an internal error that survives the retry carries the compiler's own message", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () =>
            errorResponse("internal", "internal_error", "unexpected internal exception"),
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "internal" }));
      assert.equal(response.ok, false);
      assert.equal(response.error.message, "unexpected internal exception");
      assert.equal(worker.loads, 2);
    },
  );
});

test("a rejected request is passed through without reloading the compiler", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () =>
            errorResponse("bad", "invalid_request", "params contains unknown workspace fields"),
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "bad" }));
      assert.equal(response.ok, false);
      assert.equal(response.error.code, "invalid_request");
      assert.equal(
        worker.loads,
        1,
        "a request the compiler understood and rejected is not a compiler failure",
      );
    },
  );
});

test("an allocation failure is treated as recoverable", async () => {
  await withWorker(
    {
      createModule: (attempt) =>
        attempt === 1
          ? stubModuleFactory({ mallocReturns: 0 })
          : stubModuleFactory({ respond: () => successResponse("alloc") }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: "alloc" }));
      assert.equal(response.ok, true);
      assert.equal(worker.loads, 2);
    },
  );
});

test("an ordinary JavaScript failure is reported without a pointless retry", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw new TypeError("module.stringToUTF8 is not a function");
          },
        }),
    },
    async (worker) => {
      const response = await worker.send(request());
      assert.equal(response.ok, false);
      assert.equal(response.error.code, "worker_failure");
      assert.equal(response.error.message, "module.stringToUTF8 is not a function");
      assert.equal(worker.loads, 1);
    },
  );
});

test("requests are answered in the order they arrived", async () => {
  const order = [];
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: (text) => {
            const { id } = JSON.parse(text);
            order.push(id);
            return successResponse(id);
          },
        }),
    },
    async (worker) => {
      worker.post(request({ id: 1 }));
      worker.post(request({ id: 2 }));
      worker.post(request({ id: 3 }));
      const responses = [await worker.next(), await worker.next(), await worker.next()];
      assert.deepEqual(
        responses.map((response) => response.id),
        [1, 2, 3],
      );
      assert.deepEqual(order, [1, 2, 3]);
    },
  );
});

test("a slow compiler load does not let a later request overtake an earlier one", async () => {
  await withWorker(
    {
      createModule: () =>
        new Promise((resolve) =>
          setTimeout(
            () =>
              resolve(
                stubModuleFactory({
                  respond: (text) => successResponse(JSON.parse(text).id),
                }),
              ),
            20,
          ),
        ),
    },
    async (worker) => {
      worker.post(request({ id: "first" }));
      worker.post(request({ id: "second" }));
      const responses = [await worker.next(), await worker.next()];
      assert.deepEqual(
        responses.map((response) => response.id),
        ["first", "second"],
      );
    },
  );
});

test("a failed request does not stall the requests queued behind it", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: (text) => {
            const { id } = JSON.parse(text);
            if (id === "boom") throw new Error("boom");
            return successResponse(id);
          },
        }),
    },
    async (worker) => {
      worker.post(request({ id: "boom" }));
      worker.post(request({ id: "after" }));
      const first = await worker.next();
      const second = await worker.next();
      assert.equal(first.ok, false);
      assert.equal(second.ok, true);
      assert.equal(second.id, "after");
    },
  );
});

test("a failure envelope keeps the protocol shape and the caller's id", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw new Error("failed");
          },
        }),
    },
    async (worker) => {
      const response = await worker.send(request({ id: 0 }));
      assert.equal(response.protocol, "email-markup.browser");
      assert.equal(response.version, 1);
      assert.equal(response.compiler_version, null);
      assert.equal(response.id, 0, "id 0 is a real id and must not become null");
      assert.equal(response.ok, false);
      assert.equal(typeof response.error.code, "string");
      assert.equal(typeof response.error.message, "string");
    },
  );
});

test("a message with no id still gets an answer, with a null id", async () => {
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: () => {
            throw new Error("failed");
          },
        }),
    },
    async (worker) => {
      const response = await worker.send({ nonsense: true });
      assert.equal(response.id, null);
      assert.equal(response.ok, false);
    },
  );
});

test("the worker does not judge a message it cannot understand — the compiler does", async () => {
  let seen;
  await withWorker(
    {
      createModule: () =>
        stubModuleFactory({
          respond: (text) => {
            seen = text;
            return errorResponse(null, "invalid_request", "unsupported request protocol");
          },
        }),
    },
    async (worker) => {
      const response = await worker.send({ nonsense: true });
      assert.deepEqual(JSON.parse(seen), { nonsense: true });
      assert.equal(response.error.code, "invalid_request");
    },
  );
});

test("a message that cannot be serialised is reported rather than dropped", async () => {
  await withWorker({}, async (worker) => {
    const cyclic = request({ id: "cycle" });
    cyclic.params.self = cyclic;
    const response = await worker.send(cyclic);
    assert.equal(response.ok, false);
    assert.equal(response.id, "cycle");
    assert.equal(response.error.code, "worker_failure");
  });
});

test("a compiler that fails to load is reported, and the next request tries again", async () => {
  await withWorker(
    {
      createModule: (attempt) => {
        if (attempt === 1) throw new Error("failed to fetch the compiler");
        return stubModuleFactory({ respond: () => successResponse("second") });
      },
    },
    async (worker) => {
      const first = await worker.send(request({ id: "first" }));
      assert.equal(first.ok, false);
      assert.equal(first.error.code, "worker_failure");
      assert.match(first.error.message, /failed to fetch the compiler/);

      const second = await worker.send(request({ id: "second" }));
      assert.equal(
        second.ok,
        true,
        "a failed load must not be cached — the editor would never recover",
      );
    },
  );
});

test("the worker never reaches for the page, the network, or dynamic evaluation", async () => {
  const { readFileSync } = await import("node:fs");
  const path = await import("node:path");
  const { fileURLToPath } = await import("node:url");
  const here = path.dirname(fileURLToPath(import.meta.url));
  const source = readFileSync(
    path.join(here, "../worker/email-markup.worker.mjs"),
    "utf8",
  );
  assert.doesNotMatch(source, /\beval\s*\(|new\s+Function|document\.|\bfetch\s*\(|XMLHttpRequest|importScripts/);
});
