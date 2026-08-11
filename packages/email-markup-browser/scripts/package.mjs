import { cpSync, existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const pkg = path.resolve(here, "..");
const root = path.resolve(pkg, "../..");
const build = path.join(root, "build", "browser-wasm", "browser");
const dist = path.join(pkg, "dist");
const moduleFile = path.join(build, "email-markup-browser.mjs");
const wasmFile = path.join(build, "email-markup-browser.wasm");

if (!existsSync(moduleFile) || !existsSync(wasmFile)) {
  throw new Error("WASM artifacts are missing; run npm run build:wasm first");
}

const schema = JSON.parse(
  readFileSync(path.join(root, "schema", "browser-protocol-v1.schema.json"), "utf8"),
);
if (schema.$id !== "https://email-markup.dev/schema/browser-protocol-v1.schema.json") {
  throw new Error("browser protocol schema identity is invalid");
}

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });
cpSync(moduleFile, path.join(dist, "email-markup-browser.mjs"));
cpSync(wasmFile, path.join(dist, "email-markup-browser.wasm"));
cpSync(path.join(pkg, "worker", "email-markup.worker.mjs"),
  path.join(dist, "email-markup.worker.mjs"));
cpSync(path.join(pkg, "types", "index.d.ts"), path.join(dist, "index.d.ts"));
cpSync(path.join(root, "schema", "browser-protocol-v1.schema.json"),
  path.join(dist, "browser-protocol-v1.schema.json"));
cpSync(path.join(root, "lib"), path.join(dist, "lib"), { recursive: true });
cpSync(path.join(root, "syntax"), path.join(dist, "syntax"), { recursive: true });
