import { spawnSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const extension = resolve(here, "..");
const repository = resolve(extension, "../..");
const version = (await readFile(resolve(repository, "VERSION"), "utf8")).trim();

if (!/^\d+\.\d+\.\d+$/.test(version)) {
  throw new Error("VERSION must contain a semantic version in X.Y.Z form");
}

const result = spawnSync(
  process.execPath,
  [
    resolve(extension, "node_modules/@vscode/vsce/vsce"),
    "package",
    version,
    "--no-update-package-json",
    "--no-git-tag-version",
    "--allow-missing-repository",
  ],
  { cwd: extension, stdio: "inherit" },
);

if (result.error) {
  throw result.error;
}
if (result.status !== 0) {
  process.exitCode = result.status ?? 1;
}
