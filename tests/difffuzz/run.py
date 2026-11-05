#!/usr/bin/env python3
"""Differential fuzzer driver: random E7 programs (gen.py) run on e7 and on Node.js, outputs compared.

  run.py --count 200 --seed 7                 generate, run and compare programs 0..199 of seed 7
  run.py --count 200 --seed 7 --tsc tsc       also type-check every program with `tsc --strict`
  run.py --replay prog.ts                     compare one file
  run.py --minimize prog.ts                   compare one file and minimize the mismatch

For every program: e7 (with an instruction budget and a memory limit) and node (with the shim of
tests/node_shim, TZ=UTC, type stripping or, for parameter properties, the TypeScript transform) run it; stdout and the exit behaviour (success / error) must be the same, and e7
must compile it. A mismatch is saved under --out (program, outputs, info.json) and minimized by delta
debugging over lines and brace-balanced blocks: the reproducer keeps the same kind of mismatch (same
compile error, same pair of exit statuses, a differing stdout). The program set and the order of the
results depend only on --seed, --start and --count. The exit code is 1 if there was any mismatch (or any
program that tsc rejects, with --tsc), otherwise 0.
"""
import argparse, concurrent.futures, functools, hashlib, json, os, pathlib, re, shutil, subprocess, sys, tempfile
import threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import gen  # noqa: E402

SHIM = pathlib.Path(ROOT, "tests", "node_shim", "register.mjs").resolve().as_uri()
D_TS = os.path.join(ROOT, "lib", "e7.d.ts")
# the options of tests/tsc/tsconfig.json
TSC_OPTIONS = {"target": "ES2022", "module": "ESNext", "moduleResolution": "bundler", "strict": True, "noEmit": True,
               "lib": ["ES2023", "DOM"], "moduleDetection": "force", "useDefineForClassFields": True, "types": []}
TSC_FLAGS = ["--strict", "--noEmit", "--target", "ES2022", "--module", "ESNext", "--moduleResolution", "bundler",
             "--lib", "ES2023,DOM", "--moduleDetection", "force", "--useDefineForClassFields", "--pretty", "false"]


@functools.lru_cache(None)
def typescript_module(tsc):
    """lib/typescript.js of the TypeScript package whose tsc is the command `tsc`, or None."""
    exe = shutil.which(tsc.split()[-1]) or tsc.split()[-1]
    real = os.path.realpath(exe)
    mod = os.path.join(os.path.dirname(os.path.dirname(real)), "lib", "typescript.js")
    return mod if os.path.exists(mod) else None


class Runner:
    def __init__(self, a):
        self.a = a
        self.cache = {}
        self.local = threading.local()
        self.checkers = []

    def _run(self, cmd, timeout):
        env = dict(os.environ, TZ="UTC")
        # stdout goes to a file: node writes to pipes asynchronously on some platforms and an uncaught
        # error could otherwise lose output that was already "printed"
        with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
            try:
                p = subprocess.run(cmd, stdout=out, stderr=err, timeout=timeout, env=env)
                rc = p.returncode
            except subprocess.TimeoutExpired:
                rc = None
            out.seek(0)
            err.seek(0)
            return rc, out.read().decode("utf-8", "replace"), err.read().decode("utf-8", "replace")

    def e7(self, path, timeout=None):
        a = self.a
        rc, out, err = self._run([a.e7, "--budget", str(a.budget), "--mem", str(a.mem), path], timeout or a.timeout)
        if rc is None:
            st = "timeout"
        elif rc == 0:
            st = "ok"
        elif rc == 3:
            st = "error"  # runtime error (uncaught throw or an E7 check)
        elif re.search(r":\d+:\d+: error: ", err):
            st = "compile-error"
        else:
            st = "limit"  # budget, memory, or a load failure
        return {"status": st, "rc": rc, "stdout": out, "stderr": err}

    def node(self, path, timeout=None):
        # Type stripping first: the transform (amaro / swc) that --experimental-transform-types adds has
        # miscompiled expressions (Node 24.21: `32 / (-(9 / 1000))` printed -0.0035...). It is needed only
        # for parameter properties (`constructor(public x: number)`) and enums.
        a = self.a
        rc, out, err = self._run([a.node, "--experimental-strip-types", "--no-warnings", "--import", SHIM, path],
                                 timeout or a.timeout)
        if rc not in (0, None) and "ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX" in err:
            rc, out, err = self._run([a.node, "--experimental-transform-types", "--no-warnings", "--import", SHIM,
                                      path], timeout or a.timeout)
        st = "timeout" if rc is None else "ok" if rc == 0 else "error"
        return {"status": st, "rc": rc, "stdout": out, "stderr": err}

    def both(self, src, workdir, skip_node_on_compile_error=False):
        """(e7 result, node result); node does not run when e7 fails to compile and the caller only
        needs the compile error (node's result is then None)."""
        h = hashlib.sha256(src.encode("utf-8")).hexdigest()
        if h in self.cache:
            return self.cache[h]
        path = os.path.join(workdir, f"t_{h[:16]}.ts")
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        # candidates of the minimizer can loop forever (a removed loop counter update): short time limit
        e = self.e7(path, self.a.min_timeout)
        n = None if (skip_node_on_compile_error and e["status"] == "compile-error") else self.node(path, self.a.min_timeout)
        try:
            os.remove(path)
        except OSError:
            pass
        if n is not None:
            self.cache[h] = (e, n)
        return e, n

    def tsc_ok(self, src, workdir):
        """Whether src passes tsc --strict: through a persistent checker (tscheck.mjs, one per thread) when
        the typescript module can be found from --tsc, otherwise by running tsc."""
        h = hashlib.sha256(src.encode("utf-8")).hexdigest()
        path = os.path.join(workdir, f"c_{h[:16]}.ts")
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        try:
            chk = getattr(self.local, "checker", None)
            if chk is None and typescript_module(self.a.tsc):
                chk = subprocess.Popen([self.a.node, os.path.join(HERE, "tscheck.mjs"), typescript_module(self.a.tsc),
                                        D_TS], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
                self.local.checker = chk
                self.checkers.append(chk)
            if chk is not None:
                chk.stdin.write(path + "\n")
                chk.stdin.flush()
                return chk.stdout.readline().startswith("ok")
            p = subprocess.run(self.a.tsc.split() + TSC_FLAGS + [D_TS, path], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
            return p.returncode == 0
        finally:
            os.remove(path)

    def close(self):
        for c in self.checkers:
            c.stdin.close()
            c.wait()


def first_error_line(text):
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return ""


def norm_compile_error(err):
    """The first compile error without its file:line:col prefix (numbers in internal errors, such as
    function indexes, are replaced by N: they change while the program is minimized)."""
    for line in err.splitlines():
        m = re.search(r":\d+:\d+: error: (.*)$", line)
        if m:
            msg = m.group(1)
            return re.sub(r"\d+", "N", msg) if msg.startswith("internal") else msg
    return first_error_line(err)


def norm_runtime_error(err):
    line = first_error_line(err)
    return re.sub(r"\(?[^\s()]+\.ts:\d+(:\d+)?\)?", "", line).strip()


def node_error(err):
    """The `Name: message` line of an uncaught error in Node's stderr."""
    for line in err.splitlines():
        if re.match(r"^[A-Za-z]*Error( \[\w+\])?: ", line):
            return line.strip()
    return first_error_line(err)


def signature(e7r, nr):
    """None when e7 and node agree, otherwise a tuple describing the kind of mismatch."""
    if e7r["status"] == "compile-error":
        return ("compile", norm_compile_error(e7r["stderr"]))
    es, ns = e7r["status"], nr["status"]
    if es == "timeout" or ns == "timeout":
        return ("timeout", es, ns)
    if es == "limit":
        return ("limit", first_error_line(e7r["stderr"]))
    ok_e7, ok_node = es == "ok", ns == "ok"
    if e7r["stdout"] != nr["stdout"]:
        # the errors that ended the programs are part of the kind: a minimized candidate must not stop
        # at another error (say an E7 bounds check after a guard was removed)
        return ("output", es, ns, norm_runtime_error(e7r["stderr"]) if es == "error" else "",
                node_error(nr["stderr"]) if ns == "error" else "")
    if ok_e7 != ok_node:
        return ("status", es, ns, norm_runtime_error(e7r["stderr"]) if not ok_e7 else "")
    return None


def balanced(lines):
    depth = {"{": 0, "(": 0, "[": 0}
    close = {"}": "{", ")": "(", "]": "["}
    for line in lines:
        for ch in line:
            if ch in depth:
                depth[ch] += 1
            elif ch in close:
                depth[close[ch]] -= 1
                if depth[close[ch]] < 0:
                    return False
    return all(v == 0 for v in depth.values())


def block_end(lines, i):
    """If line i opens a brace block, the index of the line that closes it, else None."""
    d = 0
    for j in range(i, len(lines)):
        d += lines[j].count("{") - lines[j].count("}")
        if j == i and d <= 0:
            return None
        if d <= 0:
            return j
    return None


def minimize(lines, interesting, deadline):
    """Delta debugging over lines (chunks of halving size), then removal of whole blocks and single lines, and
    unwrapping of blocks, each to a fixpoint."""
    def test(cand):
        return time.time() < deadline and balanced(cand) and interesting(cand)

    # chunks of halving size; after a successful removal the scan goes on at the same position
    chunk = max(1, len(lines) // 2)
    while chunk >= 1 and time.time() < deadline:
        i = 0
        while i < len(lines):
            cand = lines[:i] + lines[i + chunk:]
            if cand and test(cand):
                lines = cand
            else:
                i += chunk
        if chunk == 1:
            break
        chunk //= 2
    changed = True
    while changed and time.time() < deadline:
        changed = False
        i = 0
        while i < len(lines):
            j = block_end(lines, i)
            for (a, b) in ([(i, j)] if j is not None else []) + [(i, i)]:
                cand = lines[:a] + lines[b + 1:]
                if cand and test(cand):
                    lines, changed = cand, True
                    break
            else:
                i += 1
    # unwrap blocks: keep the body of an if/loop/try without its header and closing line
    changed = True
    while changed and time.time() < deadline:
        changed = False
        for i in range(len(lines)):
            j = block_end(lines, i)
            if j is None:
                continue
            body = [l[2:] if l.startswith("  ") else l for l in lines[i + 1:j]]
            cand = lines[:i] + body + lines[j + 1:]
            if test(cand):
                lines, changed = cand, True
                break
    return lines


def check_tsc(tsc, files, workdir):
    """Type-check files with tsc --strict (tests/tsc/tsconfig.json options); returns {file: first error}."""
    if not files:
        return {}
    cfg = {"compilerOptions": TSC_OPTIONS, "files": [D_TS] + [os.path.abspath(f) for f in files]}
    cfgpath = os.path.join(workdir, "tsconfig.json")
    with open(cfgpath, "w") as f:
        json.dump(cfg, f)
    p = subprocess.run(tsc.split() + ["-p", cfgpath, "--pretty", "false"], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, cwd=workdir)
    bad = {}
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        m = re.match(r"^(.*?\.ts)\(\d+,\d+\): error (.*)$", line)
        if m:
            fn = os.path.abspath(os.path.join(workdir, m.group(1)))
            bad.setdefault(fn, line)
    if p.returncode != 0 and not bad:
        bad["<tsc>"] = p.stdout.decode("utf-8", "replace")[:2000]
    return bad


def describe(sig):
    return " / ".join(str(x) for x in sig)


def process_mismatch(runner, a, name, src, e7r, nr, sig, outdir, workdir):
    d = os.path.join(outdir, name)
    os.makedirs(d, exist_ok=True)

    def save(prefix, text, r1, r2):
        with open(os.path.join(d, prefix + ".ts"), "w", encoding="utf-8") as f:
            f.write(text)
        for (who, r) in (("e7", r1), ("node", r2)):
            with open(os.path.join(d, f"{prefix}.{who}.out"), "w", encoding="utf-8") as f:
                f.write(r["stdout"])
            with open(os.path.join(d, f"{prefix}.{who}.err"), "w", encoding="utf-8") as f:
                f.write(r["stderr"])

    save("prog", src, e7r, nr)
    info = {"name": name, "signature": list(sig), "e7_status": e7r["status"], "node_status": nr["status"]}
    if not a.no_minimize and sig[0] != "timeout":
        def interesting(lines):
            text = "\n".join(lines) + "\n"
            e, n = runner.both(text, workdir, skip_node_on_compile_error=sig[0] == "compile")
            # with --tsc the reproducer stays valid strict TypeScript
            return signature(e, n) == sig and (not a.tsc or runner.tsc_ok(text, workdir))
        lines = minimize(src.splitlines(), interesting, time.time() + a.min_time)
        msrc = "\n".join(lines) + "\n"
        me, mn = runner.both(msrc, workdir)
        save("min", msrc, me, mn)
        info["min_lines"] = len(lines)
        info["min_e7_stdout_tail"] = me["stdout"][-400:]
        info["min_node_stdout_tail"] = mn["stdout"][-400:]
    with open(os.path.join(d, "info.json"), "w") as f:
        json.dump(info, f, indent=2)
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--e7", default=os.path.join(ROOT, "build", "e7"))
    ap.add_argument("--node", default="node")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--start", type=int, default=0, help="index of the first program")
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "difffuzz"), help="where mismatches are saved")
    ap.add_argument("--budget", type=int, default=200_000_000, help="e7 instruction budget")
    ap.add_argument("--mem", type=int, default=256 << 20, help="e7 memory limit (bytes)")
    ap.add_argument("--timeout", type=float, default=30)
    ap.add_argument("--no-ints", action="store_true", help="do not generate fixed-width integers")
    ap.add_argument("--exact-math", action="store_true",
                    help="compare sin/exp/log/... results exactly (they come from the C library in e7)")
    ap.add_argument("--no-minimize", action="store_true")
    ap.add_argument("--min-time", type=float, default=300, help="seconds per minimization")
    ap.add_argument("--min-timeout", type=float, default=5, help="time limit of one run while minimizing")
    ap.add_argument("--tsc", help="tsc command; type-check every program with tsc --strict")
    ap.add_argument("--keep", action="store_true", help="keep all generated programs in --out/programs")
    ap.add_argument("--replay", help="compare one .ts file")
    ap.add_argument("--minimize", help="compare one .ts file and minimize the mismatch")
    a = ap.parse_args()
    if not os.path.exists(a.e7):
        print(f"e7 not found: {a.e7}", file=sys.stderr)
        return 2
    runner = Runner(a)
    os.makedirs(a.out, exist_ok=True)
    workdir = tempfile.mkdtemp(prefix="e7difffuzz-")
    try:
        if a.replay or a.minimize:
            path = a.replay or a.minimize
            src = open(path, encoding="utf-8").read()
            e7r, nr = runner.both(src, workdir)
            sig = signature(e7r, nr)
            print(f"e7: {e7r['status']}  node: {nr['status']}  ->", describe(sig) if sig else "same")
            if sig and a.minimize:
                a.no_minimize = False
                name = "replay-" + os.path.splitext(os.path.basename(path))[0]
                info = process_mismatch(runner, a, name, src, e7r, nr, sig, a.out, workdir)
                print(f"minimized to {info.get('min_lines')} lines: {os.path.join(a.out, name, 'min.ts')}")
            return 1 if sig else 0

        t0 = time.time()
        progdir = os.path.join(a.out, "programs") if a.keep else os.path.join(workdir, "programs")
        os.makedirs(progdir, exist_ok=True)
        progs = []
        for i in range(a.start, a.start + a.count):
            src = gen.generate(a.seed, i, ints=not a.no_ints, exact_math=a.exact_math)
            path = os.path.join(progdir, f"s{a.seed}_{i}.ts")
            with open(path, "w", encoding="utf-8") as f:
                f.write(src)
            progs.append((i, path, src))
        bad_ts = {}
        if a.tsc:
            # in batches: one tsc process for many files is much faster than one per file
            for k in range(0, len(progs), 500):
                bad_ts.update(check_tsc(a.tsc, [p for (_, p, _) in progs[k:k + 500]], workdir))
            for fn, msg in sorted(bad_ts.items()):
                print(f"TSC {os.path.basename(fn)}: {msg}")

        def one(item):
            i, path, src = item
            return i, runner.e7(path), runner.node(path)

        results = {}
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            for i, e7r, nr in ex.map(one, progs):
                results[i] = (e7r, nr)
        mism = []
        for (i, path, src) in progs:
            e7r, nr = results[i]
            sig = signature(e7r, nr)
            if sig:
                mism.append((i, src, e7r, nr, sig))
                print(f"MISMATCH seed {a.seed} #{i}: {describe(sig)}", flush=True)
        infos = []
        if mism:
            with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
                futs = [ex.submit(process_mismatch, runner, a, f"seed{a.seed}-{i}", src, e7r, nr, sig, a.out, workdir)
                        for (i, src, e7r, nr, sig) in mism]
                infos = [f.result() for f in futs]
        statuses = {}
        for (e7r, nr) in results.values():
            k = f"e7={e7r['status']} node={nr['status']}"
            statuses[k] = statuses.get(k, 0) + 1
        summary = {"seed": a.seed, "start": a.start, "count": a.count, "mismatches": len(mism),
                   "tsc_rejected": len(bad_ts), "statuses": statuses, "seconds": round(time.time() - t0, 1),
                   "mismatch_list": infos}
        with open(os.path.join(a.out, f"summary-seed{a.seed}.json"), "w") as f:
            json.dump(summary, f, indent=2)
        print(f"{a.count} programs (seed {a.seed}, from #{a.start}): {len(mism)} mismatches"
              + (f", {len(bad_ts)} rejected by tsc" if a.tsc else "")
              + f"; {', '.join(f'{k}: {v}' for k, v in sorted(statuses.items()))}; {summary['seconds']} s")
        for info in infos:
            print(f"  {info['name']}: {describe(info['signature'])}"
                  + (f" -> {os.path.join(a.out, info['name'], 'min.ts')} ({info['min_lines']} lines)"
                     if 'min_lines' in info else ""))
        return 1 if (mism or bad_ts) else 0
    finally:
        runner.close()
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
