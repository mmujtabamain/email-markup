import { cpSync, mkdirSync, rmSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { build } from "esbuild";

await build({
  entryPoints: ["src/extension.ts"],
  outfile: "dist/node/extension.js",
  bundle: true,
  external: ["vscode"],
  format: "cjs",
  mainFields: ["module", "main"],
  platform: "node",
  target: "node20",
  sourcemap: true,
  logLevel: "info",
});

await build({
  entryPoints: ["src/web/extension.ts"],
  outfile: "dist/web/extension.js",
  bundle: true,
  external: ["vscode"],
  format: "cjs",
  mainFields: ["browser", "module", "main"],
  platform: "browser",
  target: "es2022",
  sourcemap: true,
  logLevel: "info",
});

const here = path.dirname(fileURLToPath(import.meta.url));
const extensionRoot = path.resolve(here, "..");
const browserPackage = path.resolve(extensionRoot, "../../packages/email-markup-browser/dist");
const browserTarget = path.join(extensionRoot, "browser");
rmSync(browserTarget, { recursive: true, force: true });
mkdirSync(browserTarget, { recursive: true });
cpSync(browserPackage, browserTarget, { recursive: true });
