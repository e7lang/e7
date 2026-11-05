// Persistent strict type checker for the differential fuzzer's minimizer (tests/difffuzz/run.py).
// Usage: node tscheck.mjs <typescript module (lib/typescript.js)> <lib/e7.d.ts>
// Reads one .ts path per line on stdin; answers "ok" or "error <first diagnostic>" per line. The standard
// library declarations are parsed once and reused, so a check costs tens of milliseconds instead of a
// whole tsc start-up.
import { createRequire } from "node:module";
import { createInterface } from "node:readline";

const [tsPath, e7dts] = process.argv.slice(2);
const require = createRequire(import.meta.url);
const ts = require(tsPath);
// the options of tests/tsc/tsconfig.json
const options = {
  target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ESNext, moduleResolution: ts.ModuleResolutionKind.Bundler,
  strict: true, noEmit: true, lib: ["lib.es2023.d.ts", "lib.dom.d.ts"], moduleDetection: ts.ModuleDetectionKind.Force,
  useDefineForClassFields: true, types: [],
};
const host = ts.createCompilerHost(options);
const cache = new Map();
const getSourceFile = host.getSourceFile.bind(host);
host.getSourceFile = (name, lang, onError, create) => {
  const keep = name === e7dts || name.includes("/typescript/lib/");
  if (keep && cache.has(name)) return cache.get(name);
  const sf = getSourceFile(name, lang, onError, create);
  if (keep) cache.set(name, sf);
  return sf;
};
let old;
const rl = createInterface({ input: process.stdin });
rl.on("line", (file) => {
  const program = ts.createProgram([e7dts, file], options, host, old);
  old = program;
  // diagnostics of the program file only: checking lib.dom.d.ts again for every candidate would dominate
  const sf = program.getSourceFile(file);
  const diags = [...program.getOptionsDiagnostics(), ...program.getSyntacticDiagnostics(sf),
    ...program.getSemanticDiagnostics(sf)];
  if (diags.length === 0) process.stdout.write("ok\n");
  else process.stdout.write("error " + ts.flattenDiagnosticMessageText(diags[0].messageText, " ").replace(/\n/g, " ") + "\n");
});
