import * as path from "node:path";
import { runTests } from "@vscode/test-electron";

async function main(): Promise<void> {
  const extensionDevelopmentPath = path.resolve(__dirname, "../..");
  const extensionTestsPath = path.resolve(__dirname, "suite/index");
  const workspacePath = path.resolve(
    extensionDevelopmentPath,
    "../../examples/09-css-inlining",
  );
  const vscodeExecutablePath = process.env.VSCODE_EXECUTABLE_PATH?.trim();
  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [workspacePath, "--disable-workspace-trust"],
    ...(vscodeExecutablePath ? { vscodeExecutablePath } : {}),
  });
}

void main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
