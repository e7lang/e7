// Runs the integration tests in a downloaded VS Code (headless on Linux: run under xvfb-run).
const path = require("path");
const fs = require("fs");
const os = require("os");
const { runTests } = require("@vscode/test-electron");

async function main() {
  const extensionDevelopmentPath = path.resolve(__dirname, "..");
  const extensionTestsPath = path.resolve(__dirname, "suite/index.js");
  // copy the fixtures so that tests can write files (.e7b) without touching the repository
  const workspace = fs.mkdtempSync(path.join(os.tmpdir(), "e7-vscode-"));
  fs.cpSync(path.resolve(__dirname, "fixtures"), workspace, { recursive: true });
  try {
    await runTests({
      version: process.env.VSCODE_TEST_VERSION || "stable",
      extensionDevelopmentPath,
      extensionTestsPath,
      launchArgs: [workspace, "--disable-extensions", "--disable-workspace-trust", "--skip-welcome", "--skip-release-notes"],
    });
  } catch (err) {
    console.error("integration tests failed:", err);
    process.exit(1);
  }
}

main();
