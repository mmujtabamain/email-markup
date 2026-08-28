import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../../..");
const build = path.join(root, "build", "browser-wasm");
const pkg = path.resolve(here, "..");
const emcmake = process.env.EMCMAKE || "emcmake";
const emcc = process.env.EMCC || "emcc";
const manifest = JSON.parse(readFileSync(path.join(pkg, "package.json"), "utf8"));
const toolchain = manifest.emailMarkup?.toolchain;

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

function capture(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: "utf8" });
  if (result.error?.code === "ENOENT") {
    throw new Error(
      `${command} is unavailable; install and activate Emscripten ${toolchain.emscriptenVersion}`,
    );
  }
  if (result.status !== 0) {
    throw new Error(`${command} exited with status ${result.status}`);
  }
  return `${result.stdout}${result.stderr}`;
}

function findNlohmannConfig() {
  if (process.env.EMAIL_MARKUP_WASM_NLOHMANN_DIR) {
    return process.env.EMAIL_MARKUP_WASM_NLOHMANN_DIR;
  }

  const prefixes = [];
  if (process.env.EMAIL_MARKUP_WASM_PREFIX) {
    prefixes.push(process.env.EMAIL_MARKUP_WASM_PREFIX);
  }
  for (const configuration of ["release", "debug"]) {
    const installed = path.join(root, "build", configuration, "vcpkg_installed");
    if (!existsSync(installed)) continue;
    for (const triplet of readdirSync(installed).sort()) {
      prefixes.push(path.join(installed, triplet));
    }
  }

  for (const prefix of prefixes) {
    const config = path.join(prefix, "share", "nlohmann_json");
    if (existsSync(path.join(config, "nlohmann_jsonConfig.cmake"))) return config;
  }

  throw new Error(
    "nlohmann_jsonConfig.cmake is unavailable; set EMAIL_MARKUP_WASM_NLOHMANN_DIR " +
      "to its CMake package directory",
  );
}

if (!toolchain) {
  throw new Error("package.json is missing emailMarkup.toolchain metadata");
}
const version = capture(emcc, ["--version"]);
if (
  !version.includes(` ${toolchain.emscriptenVersion} `) ||
  !version.includes(`(${toolchain.emscriptenCommit})`)
) {
  throw new Error(
    `expected Emscripten ${toolchain.emscriptenVersion} ` +
      `(${toolchain.emscriptenCommit}), received:\n${version.trim()}`,
  );
}

const configure = [
  "cmake",
  "-S",
  root,
  "-B",
  build,
  "--fresh",
  "-G",
  "Ninja",
  "-DCMAKE_BUILD_TYPE=Release",
  "-DEMAIL_MARKUP_BROWSER_ONLY=ON",
  `-Dnlohmann_json_DIR=${findNlohmannConfig()}`,
];

run(emcmake, configure);
run("cmake", ["--build", build, "--target", "email-markup-browser-wasm", "-j", "4"]);
