/**
 * Tests for the client half of the browser compiler.
 *
 * `BrowserCompiler` owns everything the editor depends on when the compiler
 * misbehaves: it routes responses by id, retires a worker that has failed,
 * backs off when failures keep arriving, and gives up on a request that never
 * comes back. None of that had a test, so a change to any of it — or a
 * regression in the worker it talks to — went unnoticed until an author lost
 * their completions.
 */
import assert from "node:assert/strict";
import test, { mock } from "node:test";

import { BrowserCompiler } from "../web/browserClient";

type Listener = (event: unknown) => void;

/**
 * The client runs in a browser worker host, where `ErrorEvent` is a global.
 * Node does not define one, so the test supplies the shape the client checks
 * for rather than making the client defensive about a global it will always
 * have in the place it actually runs.
 */
if (typeof (globalThis as { ErrorEvent?: unknown }).ErrorEvent === "undefined") {
  class NodeErrorEvent extends Event {
    readonly message: string;
    readonly error: unknown;
    constructor(type: string, init: { message?: string; error?: unknown } = {}) {
      super(type);
      this.message = init.message ?? "";
      this.error = init.error;
    }
  }
  (globalThis as { ErrorEvent?: unknown }).ErrorEvent = NodeErrorEvent;
}

/** A stand-in for the Web Worker the editor would create. */
class FakeWorker {
  static instances: FakeWorker[] = [];

  readonly sent: Array<Record<string, unknown>> = [];
  terminated = false;
  postMessageThrows: Error | undefined;
  private readonly listeners = new Map<string, Set<Listener>>();

  constructor(
    readonly url: string,
    readonly options?: { name?: string },
  ) {
    FakeWorker.instances.push(this);
  }

  static get latest(): FakeWorker {
    return FakeWorker.instances[FakeWorker.instances.length - 1];
  }

  static reset(): void {
    FakeWorker.instances = [];
  }

  addEventListener(type: string, listener: Listener): void {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type)!.add(listener);
  }

  removeEventListener(type: string, listener: Listener): void {
    this.listeners.get(type)?.delete(listener);
  }

  postMessage(message: Record<string, unknown>): void {
    if (this.postMessageThrows) throw this.postMessageThrows;
    this.sent.push(message);
  }

  terminate(): void {
    this.terminated = true;
  }

  /** Deliver a response, the way the worker's `self.postMessage` would. */
  respond(data: unknown): void {
    for (const listener of this.listeners.get("message") ?? []) listener({ data });
  }

  /** Deliver a worker-level failure, as an `ErrorEvent` the way a browser does. */
  fail(message = "worker died", error?: Error): void {
    const event = new ErrorEvent("error", { message, error });
    for (const listener of this.listeners.get("error") ?? []) listener(event);
  }

  get lastId(): number {
    return this.sent[this.sent.length - 1].id as number;
  }
}

function success(id: number, result: unknown): unknown {
  return {
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: "1.2.0",
    id,
    ok: true,
    result,
  };
}

function failure(id: number, message: string, stack?: string): unknown {
  return {
    protocol: "email-markup.browser",
    version: 1,
    compiler_version: null,
    id,
    ok: false,
    error: { code: "worker_failure", message, ...(stack ? { stack } : {}) },
  };
}

const workspace = { entry_path: "/message.em", source: "<p>Hi</p>", files: [] };

/**
 * For a request the test only cares about *sending*. Disposing the compiler
 * rejects whatever is still waiting, and an unobserved rejection would fail the
 * run for a reason that has nothing to do with what is being tested.
 */
function ignore<T>(pending: Promise<T>): Promise<void> {
  return pending.then(
    () => {},
    () => {},
  );
}

/** Installs the fake worker for one test and takes it away again afterwards. */
async function withCompiler(
  body: (compiler: BrowserCompiler, warnings: string[]) => Promise<void>,
): Promise<void> {
  const previous = (globalThis as { Worker?: unknown }).Worker;
  FakeWorker.reset();
  (globalThis as { Worker?: unknown }).Worker = FakeWorker;
  const warnings: string[] = [];
  const compiler = new BrowserCompiler("worker.mjs", (message) => warnings.push(message));
  try {
    await body(compiler, warnings);
  } finally {
    compiler.dispose();
    (globalThis as { Worker?: unknown }).Worker = previous;
  }
}

test("a request is sent as a protocol envelope with an increasing id", async () => {
  await withCompiler(async (compiler) => {
    const pending = compiler.analyze(workspace);
    const [message] = FakeWorker.latest.sent;
    assert.equal(message.protocol, "email-markup.browser");
    assert.equal(message.version, 1);
    assert.equal(message.method, "analyze");
    assert.equal(message.id, 1);
    assert.deepEqual(message.params, workspace);

    FakeWorker.latest.respond(success(1, { success: true }));
    assert.deepEqual(await pending, { success: true });

    ignore(compiler.format("/message.em", "<p>Hi</p>"));
    assert.equal(FakeWorker.latest.sent[1].id, 2, "ids must not repeat within a session");
  });
});

test("the worker is created once and reused for later requests", async () => {
  await withCompiler(async (compiler) => {
    ignore(compiler.analyze(workspace));
    ignore(compiler.analyze(workspace));
    assert.equal(FakeWorker.instances.length, 1);
    assert.equal(FakeWorker.latest.options?.name, "email-markup-compiler");
  });
});

test("each method sends its own shape, with the position merged in", async () => {
  await withCompiler(async (compiler) => {
    const position = { line: 2, character: 4 };
    ignore(compiler.format("/message.em", "text"));
    ignore(compiler.complete(workspace, position));
    ignore(compiler.hover(workspace, position));
    ignore(compiler.signature(workspace, position));
    const [format, complete, hover, signature] = FakeWorker.latest.sent;
    assert.deepEqual(format.params, { path: "/message.em", source: "text" });
    assert.deepEqual(complete.params, { ...workspace, position });
    assert.equal(hover.method, "hover");
    assert.equal(signature.method, "signature");
  });
});

test("responses are routed by id, whatever order they arrive in", async () => {
  await withCompiler(async (compiler) => {
    const first = compiler.analyze(workspace);
    const second = compiler.hover(workspace, { line: 0, character: 0 });
    FakeWorker.latest.respond(success(2, { markdown: "second" }));
    FakeWorker.latest.respond(success(1, { first: true }));
    assert.deepEqual(await second, { markdown: "second" });
    assert.deepEqual(await first, { first: true });
  });
});

test("a rejected request rejects with the compiler's message and stack", async () => {
  await withCompiler(async (compiler) => {
    const pending = compiler.analyze(workspace);
    FakeWorker.latest.respond(failure(1, "params contains unknown workspace fields", "at x"));
    await assert.rejects(pending, (error: Error) => {
      assert.equal(error.message, "params contains unknown workspace fields");
      assert.equal(error.stack, "at x");
      return true;
    });
  });
});

test("a rejected request leaves the worker in place", async () => {
  await withCompiler(async (compiler) => {
    const pending = compiler.analyze(workspace);
    FakeWorker.latest.respond(failure(1, "bad request"));
    await pending.catch(() => {});
    ignore(compiler.analyze(workspace));
    assert.equal(
      FakeWorker.instances.length,
      1,
      "a request the compiler understood and rejected is not a compiler failure",
    );
  });
});

test("a response for an unknown id is ignored rather than throwing", async () => {
  await withCompiler(async (compiler) => {
    const pending = compiler.analyze(workspace);
    FakeWorker.latest.respond(success(99, { stale: true }));
    FakeWorker.latest.respond(success(1, { fresh: true }));
    assert.deepEqual(await pending, { fresh: true });
  });
});

test("a response that is not this protocol retires the worker", async () => {
  await withCompiler(async (compiler, warnings) => {
    const pending = compiler.analyze(workspace);
    FakeWorker.latest.respond({ hello: "world" });
    await assert.rejects(pending, /unrecognized response/);
    assert.equal(FakeWorker.latest.terminated, true);
    assert.equal(warnings.length, 1);
  });
});

test("a worker failure rejects everything still waiting on it", async () => {
  await withCompiler(async (compiler) => {
    const first = compiler.analyze(workspace);
    const second = compiler.analyze(workspace);
    FakeWorker.latest.fail("out of memory");
    await assert.rejects(first, /out of memory/);
    await assert.rejects(second, /out of memory/);
  });
});

test("the next request after a failure starts a fresh worker", async () => {
  await withCompiler(async (compiler) => {
    const first = compiler.analyze(workspace);
    FakeWorker.latest.fail();
    await first.catch(() => {});
    const second = compiler.analyze(workspace);
    assert.equal(FakeWorker.instances.length, 2, "the failed worker must not be reused");
    assert.equal(FakeWorker.instances[0].terminated, true);
    FakeWorker.latest.respond(success(2, { recovered: true }));
    assert.deepEqual(await second, { recovered: true });
  });
});

test("a request that never comes back times out and retires the worker", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      const pending = compiler.analyze(workspace);
      mock.timers.tick(10_000);
      await assert.rejects(pending, /timed out/);
      assert.equal(
        FakeWorker.latest.terminated,
        true,
        "a worker still busy with a timed-out request would be behind for every later one",
      );
    });
  } finally {
    mock.timers.reset();
  }
});

test("an answered request does not later time out", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      const pending = compiler.analyze(workspace);
      FakeWorker.latest.respond(success(1, { done: true }));
      assert.deepEqual(await pending, { done: true });
      mock.timers.tick(60_000);
      assert.equal(FakeWorker.latest.terminated, false);
    });
  } finally {
    mock.timers.reset();
  }
});

test("repeated failures back off, and the backoff expires", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      // The first failure retries at once; the ones after it wait, so a document
      // that crashes the compiler on every keystroke cannot spin it in a loop.
      for (let attempt = 0; attempt < 2; attempt += 1) {
        const pending = compiler.analyze(workspace);
        FakeWorker.latest.fail();
        await pending.catch(() => {});
      }
      const third = compiler.analyze(workspace);
      FakeWorker.latest.fail();
      await third.catch(() => {});

      await assert.rejects(compiler.analyze(workspace), /restarting after a failure/);
      const workers = FakeWorker.instances.length;

      mock.timers.tick(600);
      const afterBackoff = compiler.analyze(workspace);
      assert.equal(FakeWorker.instances.length, workers + 1, "the backoff should have expired");
      FakeWorker.latest.respond(success(FakeWorker.latest.lastId, { ok: true }));
      assert.deepEqual(await afterBackoff, { ok: true });
    });
  } finally {
    mock.timers.reset();
  }
});

test("a failure long after the last one does not inherit its backoff", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      for (let attempt = 0; attempt < 3; attempt += 1) {
        const pending = compiler.analyze(workspace);
        FakeWorker.latest.fail();
        await pending.catch(() => {});
        mock.timers.tick(11_000);
      }
      // Each failure was more than ten seconds after the previous one, so none
      // of them counts as consecutive and no backoff should be in force.
      const pending = compiler.analyze(workspace);
      FakeWorker.latest.respond(success(FakeWorker.latest.lastId, { ok: true }));
      assert.deepEqual(await pending, { ok: true });
    });
  } finally {
    mock.timers.reset();
  }
});

test("a successful response clears the failure count", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      const first = compiler.analyze(workspace);
      FakeWorker.latest.fail();
      await first.catch(() => {});

      const recovered = compiler.analyze(workspace);
      FakeWorker.latest.respond(success(FakeWorker.latest.lastId, { ok: true }));
      await recovered;

      // Without the reset this failure would be the second in a row and the
      // request after it would be turned away; because the compiler answered in
      // between, it counts as the first and retries at once.
      const pending = compiler.analyze(workspace);
      FakeWorker.latest.fail();
      await pending.catch(() => {});
      const next = compiler.analyze(workspace);
      FakeWorker.latest.respond(success(FakeWorker.latest.lastId, { again: true }));
      assert.deepEqual(await next, { again: true });
    });
  } finally {
    mock.timers.reset();
  }
});

test("a second failure in a row makes the next request wait", async () => {
  mock.timers.enable({ apis: ["setTimeout", "Date"] });
  try {
    await withCompiler(async (compiler) => {
      for (let attempt = 0; attempt < 2; attempt += 1) {
        const pending = compiler.analyze(workspace);
        FakeWorker.latest.fail();
        await pending.catch(() => {});
      }
      await assert.rejects(compiler.analyze(workspace), /retrying in 500ms/);
    });
  } finally {
    mock.timers.reset();
  }
});

test("a worker that cannot be posted to is reported, not left pending", async () => {
  await withCompiler(async (compiler) => {
    const first = compiler.analyze(workspace);
    FakeWorker.latest.respond(success(1, { ok: true }));
    await first;

    const broken = FakeWorker.latest;
    broken.postMessageThrows = new Error("worker is gone");
    await assert.rejects(compiler.analyze(workspace), /worker is gone/);
    assert.equal(broken.terminated, true, "a worker that cannot be reached is retired");
  });
});

test("a disposed compiler rejects pending work and refuses more", async () => {
  await withCompiler(async (compiler) => {
    const pending = compiler.analyze(workspace);
    compiler.dispose();
    await assert.rejects(pending, /stopped/);
    await assert.rejects(compiler.analyze(workspace), /stopped/);
    assert.equal(FakeWorker.instances.length, 1, "disposing must not start another worker");
  });
});
