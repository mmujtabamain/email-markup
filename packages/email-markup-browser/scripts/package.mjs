import {
  cpSync,
  chmodSync,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const pkg = path.resolve(here, "..");
const root = path.resolve(pkg, "../..");
const build = path.join(root, "build", "browser-wasm", "browser");
const dist = path.join(pkg, "dist");
const moduleFile = path.join(build, "email-markup-browser.mjs");
const wasmFile = path.join(build, "email-markup-browser.wasm");
const packageMetadata = JSON.parse(readFileSync(path.join(pkg, "package.json"), "utf8"));

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

const assets = [
  "email-markup.worker.mjs",
  "email-markup-browser.mjs",
  "email-markup-browser.wasm",
  "browser-protocol-v1.schema.json",
  "index.d.ts",
  "lib/builtins.em",
  "lib/engines/django.emt",
  "syntax/lexical.json",
];
const packagedAssets = Object.fromEntries(
  assets.map((asset) => {
    const target = path.join(dist, asset);
    chmodSync(target, 0o644);
    return [asset, {
      bytes: statSync(target).size,
      sha256: createHash("sha256").update(readFileSync(target)).digest("hex"),
    }];
  }),
);
const artifactManifest = {
  schema: "email-markup.browser-artifacts",
  version: 1,
  compilerVersion: packageMetadata.version,
  browserProtocol: packageMetadata.emailMarkup.browserProtocol,
  toolchain: packageMetadata.emailMarkup.toolchain,
  assets: packagedAssets,
};
writeFileSync(
  path.join(dist, "manifest.json"),
  `${JSON.stringify(artifactManifest, null, 2)}\n`,
);
