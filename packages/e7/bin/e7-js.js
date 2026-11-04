#!/usr/bin/env node
// e7-js — the E7 command-line tools on WebAssembly (no native build needed).
import { readFileSync, writeFileSync } from "node:fs";
import { relative, resolve, sep } from "node:path";
import { init, formatDiagnostics, version, E7Error } from "../index.js";

const USAGE = `usage: e7-js <command> [options]
  run <file.ts|file.e7|file.e7b> [args...]  run top-level code, then main() (or --call) and print its result
      --call <fn>      call this exported function (args are parsed as JSON, otherwise passed as strings)
      --budget <n>     instruction budget (default 100000000, 0 = unlimited)
      --mem <bytes>    VM heap limit (default 67108864)
      --seed <n>       seed for Math.random
      --json           print the whole result as JSON
  check <file...>                            type check, print diagnostics (exit 1 on errors)
  compile <file> [-o out.e7b]                compile to portable bytecode
  disasm <file.ts|file.e7b>                  print the bytecode listing
  version                                    print versions`;

function fail(msg, code = 2) {
  process.stderr.write(msg + "\n");
  process.exit(code);
}

// Paths relative to the working directory, '/'-separated, as the compiler resolves imports.
const rel = (f) => relative(process.cwd(), resolve(f)).split(sep).join("/");
const readFile = (p) => {
  try {
    return readFileSync(p, "utf8");
  } catch {
    return undefined;
  }
};

function load(e7, file) {
  if (file.endsWith(".e7b")) return new Uint8Array(readFileSync(file));
  const source = readFileSync(file, "utf8");
  const r = e7.compile(source, { fileName: rel(file), readFile });
  if (!r.ok) {
    process.stderr.write(formatDiagnostics(r.diagnostics) + "\n");
    process.exit(1);
  }
  return r.bytecode;
}

function show(v) {
  if (typeof v === "string") return JSON.stringify(v);
  if (typeof v === "bigint") return v.toString();
  if (v instanceof Uint8Array) return `Uint8Array(${v.length}) ${Buffer.from(v).toString("hex")}`;
  return String(v);
}

async function main(argv) {
  const [cmd, ...rest] = argv;
  if (!cmd || cmd === "-h" || cmd === "--help" || cmd === "help") {
    process.stdout.write(USAGE + "\n");
    return 0;
  }
  if (cmd === "version" || cmd === "--version" || cmd === "-v") {
    const e7 = await init();
    process.stdout.write(`e7-js ${version} (e7 ${e7.version}, WebAssembly)\n`);
    return 0;
  }
  const opts = {};
  const pos = [];
  const valued = new Set(["--call", "--budget", "--mem", "--seed", "-o"]);
  for (let i = 0; i < rest.length; i++) {
    const a = rest[i];
    if (a === "--") {
      pos.push(...rest.slice(i + 1));
      break;
    }
    if (valued.has(a)) {
      if (i + 1 >= rest.length) fail(`e7-js: ${a} needs a value`);
      opts[a] = rest[++i];
    } else if (a === "--json") opts.json = true;
    else if (a.startsWith("-") && a !== "-") fail(`e7-js: unknown option ${a}\n${USAGE}`);
    else pos.push(a);
  }
  const e7 = await init();
  switch (cmd) {
    case "run": {
      if (!pos.length) fail(USAGE);
      const [file, ...args] = pos;
      const bytes = load(e7, file);
      const parsed = args.map((s) => {
        try {
          return JSON.parse(s);
        } catch {
          return s;
        }
      });
      const r = e7.run(bytes, {
        call: opts["--call"],
        args: opts["--call"] ? parsed : [],
        budget: opts["--budget"] !== undefined ? Number(opts["--budget"]) : undefined,
        memoryLimit: opts["--mem"] !== undefined ? Number(opts["--mem"]) : undefined,
        seed: opts["--seed"] !== undefined ? Number(opts["--seed"]) : undefined,
      });
      if (opts.json) {
        process.stdout.write(
          JSON.stringify(r, (k, v) => (typeof v === "bigint" ? v.toString() : v instanceof Uint8Array ? Buffer.from(v).toString("hex") : v), 2) + "\n",
        );
      } else {
        for (const line of r.output) process.stdout.write(line + "\n");
        if (r.ok && r.result !== undefined) process.stdout.write(show(r.result) + "\n");
        if (!r.ok) process.stderr.write(r.error + "\n");
      }
      return r.ok ? 0 : r.status === "runtime_error" ? 3 : 1;
    }
    case "check": {
      if (!pos.length) fail(USAGE);
      let bad = 0;
      for (const file of pos) {
        const d = e7.check(readFileSync(file, "utf8"), { fileName: rel(file), readFile });
        if (d.length) {
          bad++;
          process.stderr.write(formatDiagnostics(d) + "\n");
        }
      }
      if (!bad) process.stdout.write(`ok (${pos.length} file${pos.length > 1 ? "s" : ""})\n`);
      return bad ? 1 : 0;
    }
    case "compile": {
      if (pos.length !== 1) fail(USAGE);
      const bytes = load(e7, pos[0]);
      const out = opts["-o"] ?? pos[0].replace(/\.(ts|e7)$/, "") + ".e7b";
      writeFileSync(out, bytes);
      process.stdout.write(`${out} (${bytes.length} bytes)\n`);
      return 0;
    }
    case "disasm": {
      if (pos.length !== 1) fail(USAGE);
      process.stdout.write(e7.disassemble(load(e7, pos[0])));
      return 0;
    }
    default:
      fail(`e7-js: unknown command '${cmd}'\n${USAGE}`);
  }
}

main(process.argv.slice(2)).then(
  (code) => {
    process.exitCode = code ?? 0;
  },
  (e) => {
    process.stderr.write((e instanceof E7Error ? e.message : e?.stack || String(e)) + "\n");
    process.exitCode = 1;
  },
);
