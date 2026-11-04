// E7 Language — VS Code extension. Uses @e7lang/e7 (the E7 compiler + VM on WebAssembly, bundled).
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { init, formatDiagnostics } = require("@e7lang/e7");

const E7_IMPORT = /\bfrom\s*["']e7:|\bimport\s*["']e7:/;
const SCHEME = "e7-bytecode";

let e7Promise = null;
function getE7() {
  if (!e7Promise) {
    // the .wasm is copied next to dist/extension.js; pass it directly (no fetch / URL lookup)
    const wasmBinary = fs.readFileSync(path.join(__dirname, "e7wasm.wasm"));
    e7Promise = init({ wasmBinary });
    e7Promise.catch(() => (e7Promise = null));
  }
  return e7Promise;
}

function config() {
  return vscode.workspace.getConfiguration("e7");
}

/** True for .e7 files and for TypeScript files that import "e7:..." or match the `e7.files` globs. */
function isE7Document(doc) {
  if (!doc) return false;
  if (doc.languageId === "e7") return true;
  if (doc.languageId !== "typescript") return false;
  if (E7_IMPORT.test(doc.getText())) return true;
  const globs = config().get("files", []);
  return globs.some((pattern) => vscode.languages.match({ pattern }, doc) > 0);
}

function compileOptions(doc) {
  if (doc.uri.scheme !== "file") return { fileName: path.basename(doc.uri.path) || "main.ts" };
  const dir = path.dirname(doc.uri.fsPath);
  const open = new Map(vscode.workspace.textDocuments.map((d) => [d.uri.fsPath, d]));
  return {
    fileName: path.basename(doc.uri.fsPath),
    readFile: (p) => {
      const abs = path.resolve(dir, p);
      const d = open.get(abs);
      if (d) return d.getText();
      try {
        return fs.statSync(abs).isFile() ? fs.readFileSync(abs, "utf8") : undefined;
      } catch {
        return undefined;
      }
    },
  };
}

function toVsDiagnostic(doc, d, fileName) {
  const line = Math.min(Math.max(d.line - 1, 0), Math.max(doc.lineCount - 1, 0));
  const text = doc.lineAt(line).text;
  const col = Math.min(Math.max(d.column - 1, 0), text.length);
  const start = new vscode.Position(line, col);
  let range = doc.getWordRangeAtPosition(start);
  if (!range || !range.start.isEqual(start)) {
    // a string literal: underline up to the closing quote; otherwise one character
    const q = text[col];
    const close = q === '"' || q === "'" || q === "`" ? text.indexOf(q, col + 1) : -1;
    const end = close > col ? close + 1 : Math.min(col + 1, text.length);
    range = new vscode.Range(start, new vscode.Position(line, end));
  }
  const message = d.file === fileName ? d.message : `${d.file}:${d.line}:${d.column}: ${d.message}`;
  const diag = new vscode.Diagnostic(range, message, vscode.DiagnosticSeverity.Error);
  diag.source = "e7";
  return diag;
}

class Checker {
  constructor(collection, status) {
    this.collection = collection;
    this.status = status;
    this.timers = new Map();
    this.counts = new Map();
  }

  schedule(doc, delay = config().get("diagnostics.debounce", 300)) {
    const key = doc.uri.toString();
    clearTimeout(this.timers.get(key));
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      this.check(doc).catch((e) => console.error("e7: check failed", e));
    }, delay));
  }

  async check(doc) {
    if (doc.isClosed) return;
    if (!config().get("diagnostics.enable", true) || !isE7Document(doc)) {
      this.clear(doc);
      return;
    }
    const e7 = await getE7();
    const opts = compileOptions(doc);
    const result = e7.compile(doc.getText(), opts);
    const diags = result.diagnostics.map((d) => toVsDiagnostic(doc, d, opts.fileName));
    this.collection.set(doc.uri, diags);
    this.counts.set(doc.uri.toString(), diags.length);
    this.status.update();
    return diags;
  }

  clear(doc) {
    this.collection.delete(doc.uri);
    this.counts.delete(doc.uri.toString());
    this.status.update();
  }
}

class Status {
  constructor() {
    this.item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
    this.item.command = "e7.runFile";
    this.checker = null;
  }
  update() {
    const ed = vscode.window.activeTextEditor;
    const on = !!ed && isE7Document(ed.document);
    vscode.commands.executeCommand("setContext", "e7.isE7File", on);
    if (!on) return this.item.hide();
    const n = this.checker?.counts.get(ed.document.uri.toString());
    this.item.text = n ? `$(error) E7 ${n}` : "$(check) E7";
    this.item.tooltip = n ? `E7: ${n} error(s). Click to run anyway.` : "E7: no errors. Click to run (E7: Run File).";
    this.item.show();
  }
}

function targetDocument(uri) {
  if (uri instanceof vscode.Uri) {
    const open = vscode.workspace.textDocuments.find((d) => d.uri.toString() === uri.toString());
    return open ? Promise.resolve(open) : vscode.workspace.openTextDocument(uri);
  }
  const ed = vscode.window.activeTextEditor;
  return Promise.resolve(ed ? ed.document : undefined);
}

function show(v) {
  if (typeof v === "string") return JSON.stringify(v);
  if (typeof v === "bigint") return `${v}n`;
  if (v instanceof Uint8Array) return `Uint8Array(${v.length}) ${Buffer.from(v).toString("hex")}`;
  return String(v);
}

function activate(context) {
  const output = vscode.window.createOutputChannel("E7");
  const collection = vscode.languages.createDiagnosticCollection("e7");
  const status = new Status();
  const checker = new Checker(collection, status);
  status.checker = checker;

  const bytecodeDocs = new Map();
  const changed = new vscode.EventEmitter();
  const provider = {
    onDidChange: changed.event,
    provideTextDocumentContent: (uri) => bytecodeDocs.get(uri.toString()) ?? "",
  };

  async function runFile(uri) {
    const doc = await targetDocument(uri);
    if (!doc) return vscode.window.showWarningMessage("E7: open an E7 file first.");
    const e7 = await getE7();
    const cfg = config();
    const opts = compileOptions(doc);
    const t0 = Date.now();
    const r = e7.run(doc.getText(), {
      ...opts,
      budget: cfg.get("run.budget", 100000000),
      memoryLimit: cfg.get("run.memoryLimit", 64 * 1024 * 1024),
    });
    const ms = Date.now() - t0;
    output.appendLine(`[e7] run ${vscode.workspace.asRelativePath(doc.uri)}`);
    for (const line of r.output) output.appendLine(line);
    if (r.ok) {
      if (r.result !== undefined) output.appendLine(`main() => ${show(r.result)}`);
      output.appendLine(`[e7] done in ${ms} ms`);
    } else {
      output.appendLine(`[e7] ${r.status}: ${r.error}`);
    }
    output.appendLine("");
    output.show(true);
    if (r.status === "compile_error") checker.check(doc);
    return r;
  }

  async function compileFile(uri) {
    const doc = await targetDocument(uri);
    if (!doc) return vscode.window.showWarningMessage("E7: open an E7 file first.");
    if (doc.uri.scheme !== "file") return vscode.window.showWarningMessage("E7: save the file before compiling it.");
    const e7 = await getE7();
    const r = e7.compile(doc.getText(), compileOptions(doc));
    if (!r.ok) {
      checker.check(doc);
      output.appendLine(formatDiagnostics(r.diagnostics));
      output.show(true);
      vscode.window.showErrorMessage(`E7: ${r.diagnostics.length} compile error(s).`);
      return undefined;
    }
    const out = doc.uri.fsPath.replace(/\.(ts|e7)$/i, "") + ".e7b";
    fs.writeFileSync(out, r.bytecode);
    output.appendLine(`[e7] wrote ${out} (${r.bytecode.length} bytes)`);
    vscode.window.setStatusBarMessage(`E7: wrote ${path.basename(out)} (${r.bytecode.length} bytes)`, 5000);
    return out;
  }

  async function showBytecode(uri) {
    const doc = await targetDocument(uri);
    if (!doc) return vscode.window.showWarningMessage("E7: open an E7 file first.");
    const e7 = await getE7();
    const r = e7.compile(doc.getText(), compileOptions(doc));
    const text = r.ok ? e7.disassemble(r.bytecode) : formatDiagnostics(r.diagnostics) + "\n";
    const name = path.basename(doc.uri.path || "main.ts") + ".disasm";
    const vuri = vscode.Uri.from({ scheme: SCHEME, path: "/" + name, query: doc.uri.toString() });
    bytecodeDocs.set(vuri.toString(), text);
    changed.fire(vuri);
    const vdoc = await vscode.workspace.openTextDocument(vuri);
    await vscode.window.showTextDocument(vdoc, { preview: true, viewColumn: vscode.ViewColumn.Beside });
    return vdoc;
  }

  context.subscriptions.push(
    output,
    collection,
    status.item,
    changed,
    vscode.workspace.registerTextDocumentContentProvider(SCHEME, provider),
    vscode.commands.registerCommand("e7.runFile", runFile),
    vscode.commands.registerCommand("e7.compileFile", compileFile),
    vscode.commands.registerCommand("e7.showBytecode", showBytecode),
    // internal, used by the integration tests: check a document now and return its diagnostics
    vscode.commands.registerCommand("e7._check", async (uri) => checker.check(await targetDocument(uri))),
    vscode.workspace.onDidOpenTextDocument((d) => checker.schedule(d, 0)),
    vscode.workspace.onDidChangeTextDocument((e) => checker.schedule(e.document)),
    vscode.workspace.onDidCloseTextDocument((d) => checker.clear(d)),
    vscode.window.onDidChangeActiveTextEditor(() => status.update()),
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration("e7")) vscode.workspace.textDocuments.forEach((d) => checker.schedule(d, 0));
    }),
  );
  vscode.workspace.textDocuments.forEach((d) => checker.schedule(d, 0));
  status.update();
  return { isE7Document, getE7 };
}

function deactivate() {}

module.exports = { activate, deactivate };
