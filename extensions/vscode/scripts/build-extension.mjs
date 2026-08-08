import { build } from "esbuild";

await build({
  entryPoints: ["src/extension.ts"],
  outfile: "dist/extension.js",
  bundle: true,
  external: ["vscode"],
  format: "cjs",
  mainFields: ["module", "main"],
  platform: "node",
  target: "node20",
  sourcemap: true,
  logLevel: "info",
});
