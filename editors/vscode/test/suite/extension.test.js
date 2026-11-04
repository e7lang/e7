// Integration tests: open the fixtures in VS Code and check diagnostics and commands.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const HMAC = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";
const root = () => vscode.workspace.workspaceFolders[0].uri.fsPath;
const uriOf = (f) => vscode.Uri.file(path.join(root(), f));
const e7Diags = (uri) => vscode.languages.getDiagnostics(uri).filter((d) => d.source === "e7");

async function waitFor(fn, what, timeout = 20000) {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > timeout) throw new Error(`timed out waiting for ${what}`);
    await new Promise((r) => setTimeout(r, 100));
  }
}

async function open(f) {
  const doc = await vscode.workspace.openTextDocument(uriOf(f));
  await vscode.window.showTextDocument(doc);
  return doc;
}

describe("E7 extension", function () {
  before(async () => {
    const ext = vscode.extensions.getExtension("e7lang.e7");
    assert.ok(ext, "extension is installed");
    await open("good.e7");
    await ext.activate();
    assert.ok(ext.isActive);
  });

  it("registers the e7 language for .e7 files", async () => {
    const doc = await open("good.e7");
    assert.strictEqual(doc.languageId, "e7");
  });

  it("reports a type error with the right range", async () => {
    const doc = await open("bad.e7");
    const diags = await waitFor(() => e7Diags(doc.uri).length && e7Diags(doc.uri), "diagnostics on bad.e7");
    assert.strictEqual(diags.length, 1);
    const d = diags[0];
    assert.strictEqual(d.severity, vscode.DiagnosticSeverity.Error);
    assert.match(d.message, /not assignable/);
    const line = doc.lineAt(2).text; // let total: number = "ten";
    assert.strictEqual(d.range.start.line, 2);
    assert.strictEqual(d.range.start.character, line.indexOf('"ten"'));
    assert.ok(d.range.end.isAfter(d.range.start));
    assert.strictEqual(d.range.end.line, 2);
  });

  it("uses UTF-16 columns after non-ASCII text", async () => {
    const doc = await open("unicode.e7");
    const diags = await waitFor(() => e7Diags(doc.uri).length && e7Diags(doc.uri), "diagnostics on unicode.e7");
    const line = doc.lineAt(diags[0].range.start.line).text;
    assert.strictEqual(diags[0].range.start.character, line.indexOf("'a'"));
  });

  it("reports no diagnostics for valid code", async () => {
    const doc = await open("good.e7");
    const diags = await vscode.commands.executeCommand("e7._check", doc.uri);
    assert.deepStrictEqual(diags, []);
    assert.strictEqual(e7Diags(doc.uri).length, 0);
  });

  it("checks .ts files that import e7: modules", async () => {
    const doc = await open("sign.ts");
    assert.strictEqual(doc.languageId, "typescript");
    const diags = await vscode.commands.executeCommand("e7._check", doc.uri);
    assert.deepStrictEqual(diags, []);
    const bad = await open("bad-import.ts");
    await waitFor(() => e7Diags(bad.uri).length, "diagnostics on bad-import.ts");
  });

  it("ignores ordinary .ts files unless they match e7.files", async () => {
    const doc = await open("scripts/plain.ts");
    assert.strictEqual(await vscode.commands.executeCommand("e7._check", doc.uri), undefined);
    assert.strictEqual(e7Diags(doc.uri).length, 0);
    await vscode.workspace.getConfiguration("e7").update("files", ["**/scripts/*.ts"], vscode.ConfigurationTarget.Global);
    try {
      await waitFor(() => e7Diags(doc.uri).length, "diagnostics on scripts/plain.ts via e7.files");
      assert.match(e7Diags(doc.uri)[0].message, /any/);
    } finally {
      await vscode.workspace.getConfiguration("e7").update("files", undefined, vscode.ConfigurationTarget.Global);
    }
    await waitFor(() => e7Diags(doc.uri).length === 0, "diagnostics cleared");
  });

  it("updates diagnostics on change (debounced)", async () => {
    const doc = await open("good.e7");
    const ed = vscode.window.activeTextEditor;
    await ed.edit((b) => b.insert(new vscode.Position(0, 0), "let broken: string = 1;\n"));
    const diags = await waitFor(() => e7Diags(doc.uri).length && e7Diags(doc.uri), "diagnostics after edit");
    assert.strictEqual(diags[0].range.start.line, 0);
    await vscode.commands.executeCommand("undo");
    await waitFor(() => e7Diags(doc.uri).length === 0, "diagnostics cleared after undo");
  });

  it("E7: Run File prints output and main() result", async () => {
    await open("good.e7");
    const r = await vscode.commands.executeCommand("e7.runFile");
    assert.strictEqual(r.status, "ok", r.error);
    assert.deepStrictEqual(r.output, ["hello from E7 👋", "sum = 6"]);
    assert.strictEqual(r.result, 42);
    const s = await vscode.commands.executeCommand("e7.runFile", uriOf("sign.ts"));
    assert.strictEqual(s.result, HMAC);
  });

  it("E7: Run File reports runtime errors and imports work", async () => {
    const r = await vscode.commands.executeCommand("e7.runFile", uriOf("app.e7"));
    assert.strictEqual(r.status, "ok", r.error);
    assert.strictEqual(r.result, 42);
    const b = await vscode.commands.executeCommand("e7.runFile", uriOf("bad.e7"));
    assert.strictEqual(b.status, "compile_error");
  });

  it("E7: Compile to Bytecode writes a .e7b next to the file", async () => {
    const out = await vscode.commands.executeCommand("e7.compileFile", uriOf("sign.ts"));
    assert.strictEqual(out, path.join(root(), "sign.e7b"));
    assert.ok(fs.statSync(out).size > 100);
  });

  it("E7: Show Bytecode opens a virtual document", async () => {
    const doc = await vscode.commands.executeCommand("e7.showBytecode", uriOf("good.e7"));
    assert.strictEqual(doc.uri.scheme, "e7-bytecode");
    assert.match(doc.getText(), /func #\d+ main/);
  });
});
