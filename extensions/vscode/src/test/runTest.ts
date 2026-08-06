import * as path from "node:path";
import { runTests } from "@vscode/test-electron";

async function main(): Promise<void> {
  const extensionDevelopmentPath = path.resolve(__dirname, "../..");
  const extensionTestsPath = path.resolve(__dirname, "suite/index");
  const workspacePath = path.resolve(extensionDevelopmentPath, "../../examples/component_gallery");
  await runTests({
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [workspacePath, "--disable-workspace-trust"],
  });
}

void main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
