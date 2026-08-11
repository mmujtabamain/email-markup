import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../../..");
const build = path.join(root, "build", "browser-wasm");
const emcmake = process.env.EMCMAKE || "emcmake";

function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: "inherit" });
  if (result.error?.code === "ENOENT") {
    throw new Error(
      `${command} is unavailable; install and activate the Emscripten SDK before building`,
    );
  }
  if (result.status !== 0) {
    throw new Error(`${command} exited with status ${result.status}`);
  }
}

const configure = [
  "cmake",
  "-S",
  root,
  "-B",
  build,
  "-G",
  "Ninja",
  "-DCMAKE_BUILD_TYPE=Release",
  "-DEMAIL_MARKUP_BROWSER_ONLY=ON",
];

if (process.env.EMAIL_MARKUP_WASM_PREFIX) {
  configure.push(`-DCMAKE_PREFIX_PATH=${process.env.EMAIL_MARKUP_WASM_PREFIX}`);
} else {
  const nativePrefix = path.join(root, "build", "debug", "vcpkg_installed", "arm64-osx");
  if (existsSync(nativePrefix)) {
    configure.push(`-DCMAKE_PREFIX_PATH=${nativePrefix}`);
  }
}

run(emcmake, configure);
run("cmake", ["--build", build, "--target", "email-markup-browser-wasm", "-j", "4"]);
