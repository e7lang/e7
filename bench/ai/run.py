#!/usr/bin/env python3
"""AI benchmark for E7: validate the tasks, run a model or a coding agent on them, report.

    python3 bench/ai/run.py validate [--tasks w01,p]
    python3 bench/ai/run.py run --backend claude-code --model sonnet --skill off --runs 3 --jobs 4
    python3 bench/ai/run.py report bench/ai/results/*.jsonl

See bench/ai/README.md.
"""
import argparse
import collections
import concurrent.futures
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "bench" / "ai"
TASKS = BENCH / "tasks"
SKILL = ROOT / "skills" / "e7"

GROUPS = {"w": "write", "p": "port", "e": "extend", "f": "fix"}

PREAMBLE = """\
You are working in a directory of an E7 project. E7 is a strict subset of TypeScript that is compiled to bytecode \
and run by the e7vm virtual machine inside apps. These tools are on the PATH:
  e7c <file.ts> --check    compile only and print errors
  e7 <file.ts>             compile and run
  e7 test                  run the tests in every *.test.ts file in this directory
When you are done, solution.ts in this directory is graded by more tests that you do not see.

"""

API_PREAMBLE = """\
You write code in E7, a strict subset of TypeScript that is compiled to bytecode and run by the e7vm virtual \
machine inside apps. Answer with the complete content of solution.ts in a single ```ts code block. After each \
answer the E7 compiler checks your code and the example tests run; you get the errors back and can answer again. \
When the code compiles and the examples pass, solution.ts is graded by more tests that you do not see.
"""

# Anthropic API prices in USD per million tokens: input, output, cache read. Cache writes cost 1.25x input.
PRICES = {
    "claude-opus-5-5": (4.0, 20.0, 0.20),
    "claude-sonnet-5-5": (2.0, 10.0, 0.20),
    "claude-haiku-5-5": (0.10, 0.50, 0.01),
}
API_MODELS = {"opus": "claude-opus-5-5", "sonnet": "claude-sonnet-5-5", "haiku": "claude-haiku-5-5"}

ERROR_LINE = re.compile(r"^(?:.*?)(\d+):(\d+): error: (.*)$", re.M)


# ----------------------------------------------------------------------------------------------- tasks


class Task:
    def __init__(self, path: Path):
        self.path = path
        self.meta = json.loads((path / "task.json").read_text())
        self.id = self.meta["id"]
        self.group = self.meta["group"]
        self.file = self.meta.get("file", "solution.ts")

    @property
    def prompt(self):
        return (self.path / "prompt.md").read_text()

    def starter_files(self):
        d = self.path / "starter"
        return sorted(p for p in d.iterdir() if p.is_file()) if d.is_dir() else []

    def hidden_count(self):
        return len(re.findall(r"^export (?:async )?function test", (self.path / "hidden.test.ts").read_text(), re.M))


def load_tasks(spec):
    keys = [s.strip() for s in (spec or "").split(",") if s.strip()]
    return [Task(p) for p in sorted(TASKS.iterdir())
            if (p / "task.json").is_file() and (not keys or any(p.name.startswith(k) for k in keys))]


# ---------------------------------------------------------------------------------------- e7 helpers


def tool(bin_dir, name):
    p = Path(bin_dir) / name
    if not p.exists():
        sys.exit(f"{p} not found (build E7 first, or pass --bin)")
    return str(p)


def e7_test(bin_dir, cwd, files, timeout=300):
    """Runs `e7 test --json files` in cwd; returns (passed, total, failures, compile_error)."""
    try:
        r = subprocess.run([tool(bin_dir, "e7"), "test", "--json", *files], cwd=cwd, capture_output=True,
                           text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 0, 0, ["timeout"], None
    try:
        out = json.loads(r.stdout)
    except json.JSONDecodeError:
        return 0, 0, [(r.stdout + r.stderr).strip()[:2000]], None
    compile_error = "".join(f["compileError"] for f in out["files"] if f.get("compileError")) or None
    failures = [f"{t['name']}: {t['error']}" for t in out["tests"] if not t["ok"]]
    return out["passed"], out["passed"] + len(failures), failures, compile_error


def e7_check(bin_dir, cwd, file):
    """Compiles file with e7c --check; returns the error messages (empty when it compiles) and the raw text."""
    r = subprocess.run([tool(bin_dir, "e7c"), file, "--check", "--no-excerpt"], cwd=cwd, capture_output=True,
                       text=True, timeout=120)
    text = (r.stdout + r.stderr).strip()
    if r.returncode == 0:
        return [], text
    msgs = [m.group(3) for m in ERROR_LINE.finditer(text)]
    return msgs or [text[:500]], text


def grade(bin_dir, task, solution_text):
    """Runs the hidden tests against solution_text; returns (passed, total, failures)."""
    total = task.hidden_count()
    if solution_text is None:
        return 0, total, ["no solution.ts"]
    with tempfile.TemporaryDirectory(prefix="e7bench-grade-") as d:
        (Path(d) / task.file).write_text(solution_text)
        shutil.copy(task.path / "hidden.test.ts", d)
        passed, _, failures, compile_error = e7_test(bin_dir, d, ["hidden.test.ts"])
    if compile_error:
        return 0, total, [compile_error.strip()]
    return passed, total, failures


# ------------------------------------------------------------------------------------------ validate


def cmd_validate(args):
    tasks = load_tasks(args.tasks)
    bad = 0
    print(f"{'task':32} {'group':7} {'hidden':>6}  result")
    for t in tasks:
        problems, notes = [], []
        for f in ("prompt.md", "hidden.test.ts", "reference/solution.ts"):
            if not (t.path / f).is_file():
                problems.append(f"missing {f}")
        if t.group not in GROUPS.values() or GROUPS.get(t.id[0]) != t.group:
            problems.append(f"group '{t.group}' does not match id prefix")
        if problems:
            bad += 1
            print(f"{t.id:32} {t.group:7} {'':>6}  FAIL: {'; '.join(problems)}")
            continue
        visible = (t.path / "starter" / "solution.test.ts").is_file()
        with tempfile.TemporaryDirectory(prefix="e7bench-val-") as d:
            shutil.copy(t.path / "reference" / "solution.ts", Path(d) / t.file)
            shutil.copy(t.path / "hidden.test.ts", d)
            files = ["hidden.test.ts"]
            if visible:
                shutil.copy(t.path / "starter" / "solution.test.ts", d)
                files.append("solution.test.ts")
            passed, total, failures, compile_error = e7_test(args.bin, d, files)
            if compile_error:
                problems.append(f"reference does not compile: {compile_error.strip()[:300]}")
            elif failures or total == 0:
                problems.append(f"reference fails {len(failures)}/{total}: {failures[:2]}")
        starter = t.path / "starter" / "solution.ts"
        if t.group in ("fix", "extend"):
            if not starter.is_file():
                problems.append("starter/solution.ts missing")
            else:
                with tempfile.TemporaryDirectory(prefix="e7bench-val-") as d:
                    shutil.copy(starter, d)
                    errs, _ = e7_check(args.bin, d, "solution.ts")
                    if errs:
                        problems.append(f"starter does not compile: {errs[:2]}")
                    elif t.group == "fix":
                        hp, ht, _ = grade(args.bin, t, starter.read_text())
                        if hp == ht:
                            problems.append("starter passes every hidden test (the bug is not tested)")
                        notes.append(f"starter fails {ht - hp}/{ht} hidden")
                        if visible:
                            shutil.copy(t.path / "starter" / "solution.test.ts", d)
                            _, _, vf, _ = e7_test(args.bin, d, ["solution.test.ts"])
                            if vf:
                                problems.append(f"starter fails visible tests: {vf[:2]}")
                    else:
                        hp, ht, _ = grade(args.bin, t, starter.read_text())
                        notes.append(f"starter passes {hp}/{ht} hidden")
        if t.group == "port":
            orig = t.path / "starter" / "original.ts"
            if not orig.is_file():
                problems.append("starter/original.ts missing")
            else:
                with tempfile.TemporaryDirectory(prefix="e7bench-val-") as d:
                    shutil.copy(orig, d)
                    errs, _ = e7_check(args.bin, d, "original.ts")
                    notes.append(f"original.ts: {len(errs)} E7 errors")
        if problems:
            bad += 1
            print(f"{t.id:32} {t.group:7} {t.hidden_count():>6}  FAIL: {'; '.join(problems)}")
        else:
            print(f"{t.id:32} {t.group:7} {t.hidden_count():>6}  ok  {'; '.join(notes)}")
    counts = collections.Counter(t.group for t in tasks)
    print(f"\n{len(tasks)} tasks ({', '.join(f'{n} {g}' for g, n in sorted(counts.items()))}), {bad} failing")
    return 1 if bad else 0


# ------------------------------------------------------------------------------------- claude-code


def compile_events(transcript):
    """From a claude-code stream-json transcript: the compiler outputs the agent saw, in order (list of
    error-message lists; an empty list is a clean compile)."""
    pending = {}
    out = []
    for ev in transcript:
        msg = ev.get("message") or {}
        content = msg.get("content")
        if not isinstance(content, list):
            continue
        for block in content:
            if ev.get("type") == "assistant" and block.get("type") == "tool_use" and block.get("name") == "Bash":
                cmd = block.get("input", {}).get("command", "")
                if re.search(r"(^|[\s;&|/])e7c?\s", cmd + " "):
                    pending[block.get("id")] = cmd
            elif ev.get("type") == "user" and block.get("type") == "tool_result" and block.get("tool_use_id") in pending:
                cmd = pending.pop(block["tool_use_id"])
                c = block.get("content")
                text = c if isinstance(c, str) else "".join(x.get("text", "") for x in c or [] if isinstance(x, dict))
                errs = [m.group(3) for m in ERROR_LINE.finditer(text)]
                if errs or re.search(r"(--check|^e7c|\be7c\b|\be7 test\b|\be7 \S+\.ts)", cmd):
                    out.append(errs)
    return out


def run_claude_code(task, args, workdir, log_path):
    for f in task.starter_files():
        shutil.copy(f, workdir)
    prompt = PREAMBLE + task.prompt
    cmd = ["claude", "-p", prompt, "--model", args.model, "--output-format", "stream-json", "--verbose",
           "--permission-mode", "acceptEdits", "--no-session-persistence", "--max-turns", str(args.max_turns),
           "--allowedTools", "Read", "Write", "Edit", "Glob", "Grep", "Bash(e7c:*)", "Bash(e7:*)",
           "Bash(ls:*)", "Bash(cat:*)"]
    if args.skill == "on":
        cmd += ["--plugin-dir", str(ROOT)]
    else:
        cmd += ["--disable-slash-commands"]
    env = dict(os.environ)
    env["PATH"] = str(Path(args.bin).resolve()) + os.pathsep + env.get("PATH", "")
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=workdir, env=env, capture_output=True, text=True, timeout=args.timeout)
        stdout, timed_out = r.stdout, False
    except subprocess.TimeoutExpired as e:
        stdout, timed_out = (e.stdout or b"").decode() if isinstance(e.stdout, bytes) else (e.stdout or ""), True
    seconds = time.time() - t0
    log_path.write_text(stdout)
    transcript = []
    for line in stdout.splitlines():
        try:
            transcript.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    result = next((e for e in reversed(transcript) if e.get("type") == "result"), {})
    usage = result.get("usage") or {}
    compiles = compile_events(transcript)
    sol = Path(workdir) / task.file
    return {
        "solution": sol.read_text() if sol.is_file() else None,
        "first_compile_ok": (not compiles[0]) if compiles else None,
        "compile_failures": sum(1 for c in compiles if c),
        "errors": [m for c in compiles for m in c],
        "turns": result.get("num_turns"),
        "input_tokens": (usage.get("input_tokens") or 0) + (usage.get("cache_read_input_tokens") or 0)
                        + (usage.get("cache_creation_input_tokens") or 0),
        "output_tokens": usage.get("output_tokens") or 0,
        "cost_usd": result.get("total_cost_usd"),
        "seconds": round(seconds, 1),
        "agent_error": ("timeout" if timed_out else
                        (result.get("result") if result.get("is_error") else None) if result else "no result event"),
    }


# ------------------------------------------------------------------------------------ anthropic-api

_skill_text = None


def skill_text():
    global _skill_text
    if _skill_text is None:
        parts = [SKILL / "SKILL.md"] + sorted((SKILL / "references").glob("*.md")) + [SKILL / "references" / "e7.d.ts"]
        _skill_text = "\n\n".join(f"<file name=\"{p.relative_to(SKILL)}\">\n{p.read_text()}\n</file>" for p in parts)
    return _skill_text


def extract_code(text):
    blocks = re.findall(r"```(?:ts|typescript)\s*\n(.*?)```", text, re.S)
    return blocks[-1] if blocks else None


def run_api(task, args, workdir, log_path):
    import anthropic  # pip install anthropic

    client = anthropic.Anthropic()
    model = API_MODELS.get(args.model, args.model)
    system = API_PREAMBLE
    if args.skill == "on":
        system += "\nReference documentation for E7:\n\n" + skill_text()
    starter = "".join(f"\n\nFile `{f.name}`:\n```ts\n{f.read_text()}```" for f in task.starter_files())
    messages = [{"role": "user", "content": task.prompt + starter}]
    for f in task.starter_files():
        shutil.copy(f, workdir)
    tokens = collections.Counter()
    compiles, solution, rounds, refusal = [], None, 0, None
    log = []
    t0 = time.time()
    while rounds < args.rounds:
        rounds += 1
        params = dict(model=model, max_tokens=32000, system=system, messages=messages,
                      cache_control={"type": "ephemeral"})
        if args.effort:
            params["output_config"] = {"effort": args.effort}
        # No refusal fallbacks: a fallback would answer with a different model than the one measured.
        with client.messages.stream(**params) as stream:
            msg = stream.get_final_message()
        u = msg.usage
        tokens["input"] += u.input_tokens or 0
        tokens["cache_read"] += u.cache_read_input_tokens or 0
        tokens["cache_write"] += u.cache_creation_input_tokens or 0
        tokens["output"] += u.output_tokens or 0
        text = "".join(b.text for b in msg.content if b.type == "text")
        log.append({"round": rounds, "stop_reason": msg.stop_reason, "text": text})
        messages.append({"role": "assistant", "content": msg.content})
        if msg.stop_reason == "refusal":
            refusal = "refusal"
            break
        code = extract_code(text)
        if code is None:
            messages.append({"role": "user", "content": "Please answer with the complete solution.ts in a single ```ts code block."})
            continue
        solution = code
        (Path(workdir) / task.file).write_text(code)
        errs, raw = e7_check(args.bin, workdir, task.file)
        compiles.append(errs)
        log[-1]["compile"] = raw
        if errs:
            messages.append({"role": "user", "content": f"e7c reported:\n```\n{raw}\n```"})
            continue
        if (Path(workdir) / "solution.test.ts").is_file():
            _, _, failures, compile_error = e7_test(args.bin, workdir, ["solution.test.ts"])
            if compile_error or failures:
                report = compile_error or "\n".join(failures)
                log[-1]["tests"] = report
                messages.append({"role": "user", "content": f"The code compiles, but `e7 test` reported:\n```\n{report}\n```"})
                continue
        break
    log_path.write_text(json.dumps(log, indent=1))
    pin, pout, pread = PRICES.get(model, (0, 0, 0))
    cost = (tokens["input"] * pin + tokens["cache_write"] * pin * 1.25 + tokens["cache_read"] * pread
            + tokens["output"] * pout) / 1e6
    return {
        "solution": solution,
        "first_compile_ok": (not compiles[0]) if compiles else None,
        "compile_failures": sum(1 for c in compiles if c),
        "errors": [m for c in compiles for m in c],
        "turns": rounds,
        "input_tokens": tokens["input"] + tokens["cache_read"] + tokens["cache_write"],
        "output_tokens": tokens["output"],
        "cost_usd": round(cost, 4),
        "seconds": round(time.time() - t0, 1),
        "agent_error": refusal,
    }


def run_reference(task, args, workdir, log_path):
    """Dry run: the reference solution stands in for the model's answer (checks the harness, costs nothing)."""
    code = (task.path / "reference" / task.file).read_text()
    (Path(workdir) / task.file).write_text(code)
    errs, raw = e7_check(args.bin, workdir, task.file)
    log_path.write_text(raw)
    return {"solution": code, "first_compile_ok": not errs, "compile_failures": 1 if errs else 0, "errors": errs,
            "turns": 1, "input_tokens": 0, "output_tokens": 0, "cost_usd": 0.0, "seconds": 0.0, "agent_error": None}


# ------------------------------------------------------------------------- external agents (prepare / collect)

WRAPPER = """#!/bin/bash
# Logs every call and its output for the benchmark, then behaves like {name}.
set -o pipefail
printf '### {name} %s\\n' "$*" >> "{log}"
"{real}" "$@" 2>&1 | tee -a "{log}"
"""

EXTERNAL_PROMPT = """\
You are working on a task in the directory {workdir} (work only there). The project is written in E7, a strict \
subset of TypeScript that is compiled to bytecode and run by the e7vm virtual machine inside apps. Use these tools \
(run them from {workdir}):
  {bin}/e7c <file.ts> --check    compile only and print errors
  {bin}/e7 <file.ts>             compile and run
  {bin}/e7 test                  run the tests in every *.test.ts file in the current directory
{docs}When you are done, {workdir}/solution.ts is graded by more tests that you do not see. Do not look for them, \
and do not read other files on this machine{docs_exception}. Finish with a one-line summary.

The task:

"""


def cmd_prepare(args):
    """Sets up working directories for an agent run outside this script (e.g. agents started by another tool)."""
    tasks = load_tasks(args.tasks)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = (Path(args.dir) if args.dir else BENCH / "results" / f"{stamp}-external-{args.label}").resolve()
    real = Path(args.bin).resolve()
    manifest = []
    for i in range(args.runs):
        for t in tasks:
            key = f"{t.id}-{i}"
            wd, bd, log = base / "work" / key, base / "bin" / key, base / "logs" / f"{key}.e7log"
            wd.mkdir(parents=True)
            bd.mkdir(parents=True)
            log.parent.mkdir(exist_ok=True)
            for f in t.starter_files():
                shutil.copy(f, wd)
            for name in ("e7", "e7c"):
                w = bd / name
                w.write_text(WRAPPER.format(name=name, log=log, real=real / name))
                w.chmod(0o755)
            docs = (f"Documentation for E7 is in {SKILL} (start with SKILL.md).\n" if args.skill == "on" else "")
            prompt = EXTERNAL_PROMPT.format(workdir=wd, bin=bd, docs=docs,
                                            docs_exception=" except that documentation" if docs else "") + t.prompt
            (base / "prompts").mkdir(exist_ok=True)
            (base / "prompts" / f"{key}.md").write_text(prompt)
            manifest.append({"key": key, "task": t.id, "run": i, "workdir": str(wd), "prompt": str(base / "prompts" / f"{key}.md")})
    (base / "manifest.json").write_text(json.dumps({"skill": args.skill, "label": args.label, "runs": manifest}, indent=1))
    print(base)
    return 0


def log_compile_events(text):
    events = []
    for chunk in text.split("### ")[1:]:
        head, _, body = chunk.partition("\n")
        errs = [m.group(3) for m in ERROR_LINE.finditer(body)]
        if errs or head.startswith("e7c") or head.startswith("e7 "):
            events.append(errs)
    return events


def cmd_collect(args):
    """Grades a prepared directory after the agents finished; writes <dir>.jsonl and prints the report.
    usage.json in the directory may give {key: {"tokens": n, "seconds": s, "turns": t}} per run."""
    base = Path(args.dir).resolve()
    man = json.loads((base / "manifest.json").read_text())
    usage = json.loads((base / "usage.json").read_text()) if (base / "usage.json").is_file() else {}
    version = subprocess.run([tool(args.bin, "e7"), "--version"], capture_output=True, text=True).stdout.strip()
    tasks = {t.id: t for t in load_tasks(None)}
    rows = []
    for m in man["runs"]:
        t = tasks[m["task"]]
        sol_path = Path(m["workdir"]) / t.file
        sol = sol_path.read_text() if sol_path.is_file() else None
        if t.group in ("fix", "extend") and sol == (t.path / "starter" / t.file).read_text():
            pass  # unchanged starter: graded as is
        log = base / "logs" / f"{m['key']}.e7log"
        events = log_compile_events(log.read_text()) if log.is_file() else []
        hp, ht, failures = grade(args.bin, t, sol)
        u = usage.get(m["key"], {})
        rows.append({"task": t.id, "group": t.group, "run": m["run"], "backend": "external", "model": args.model,
                     "skill": man["skill"], "effort": None, "e7": version, "passed": ht > 0 and hp == ht,
                     "hidden_passed": hp, "hidden_total": ht, "hidden_failures": failures[:5],
                     "first_compile_ok": (not events[0]) if events else None,
                     "compile_failures": sum(1 for e in events if e), "errors": [x for e in events for x in e],
                     "turns": u.get("turns"), "input_tokens": u.get("tokens"), "output_tokens": None,
                     "cost_usd": u.get("cost_usd"), "seconds": u.get("seconds"), "agent_error": None})
    out = base.with_suffix(".jsonl")
    out.write_text("".join(json.dumps(r) + "\n" for r in rows))
    for r in rows:
        print(f"{'PASS' if r['passed'] else 'fail'} {r['task']} #{r['run']}  hidden {r['hidden_passed']}/{r['hidden_total']}"
              f"  compile failures {r['compile_failures']}")
    print()
    print(report(rows))
    print(f"\n-> {out}")
    return 0


# --------------------------------------------------------------------------------------------- run


def cmd_run(args):
    tasks = load_tasks(args.tasks)
    if not tasks:
        sys.exit("no tasks")
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    name = f"{stamp}-{args.backend}-{args.model}-skill{args.skill}"
    out_dir = BENCH / "results"
    logs = out_dir / name
    logs.mkdir(parents=True, exist_ok=True)
    out_file = Path(args.out) if args.out else out_dir / f"{name}.jsonl"
    version = subprocess.run([tool(args.bin, "e7"), "--version"], capture_output=True, text=True).stdout.strip()
    runner = {"claude-code": run_claude_code, "anthropic-api": run_api, "reference": run_reference}[args.backend]
    lock = threading.Lock()
    jobs = [(t, i) for i in range(args.runs) for t in tasks]
    print(f"{len(jobs)} runs ({len(tasks)} tasks x {args.runs}), {args.jobs} at a time -> {out_file}")

    def one(job):
        task, i = job
        with tempfile.TemporaryDirectory(prefix=f"e7bench-{task.id}-") as wd:
            try:
                r = runner(task, args, wd, logs / f"{task.id}-{i}.log")
            except Exception as e:  # noqa: BLE001 - record and go on
                r = {"solution": None, "agent_error": f"{type(e).__name__}: {e}"}
        sol = r.pop("solution", None)
        if sol is not None:
            (logs / f"{task.id}-{i}.solution.ts").write_text(sol)
        hp, ht, failures = grade(args.bin, task, sol)
        row = {"task": task.id, "group": task.group, "run": i, "backend": args.backend, "model": args.model,
               "skill": args.skill, "effort": args.effort, "e7": version, "passed": ht > 0 and hp == ht,
               "hidden_passed": hp, "hidden_total": ht, "hidden_failures": failures[:5], **r}
        with lock:
            with open(out_file, "a") as f:
                f.write(json.dumps(row) + "\n")
            mark = "PASS" if row["passed"] else "fail"
            print(f"{mark} {task.id} #{i}  hidden {hp}/{ht}  compile failures {row.get('compile_failures')}  "
                  f"turns {row.get('turns')}  ${row.get('cost_usd')}  {row.get('seconds')}s"
                  + (f"  [{row['agent_error'][:80]}]" if row.get("agent_error") else ""), flush=True)
        return row

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        rows = list(ex.map(one, jobs))
    print()
    print(report(rows))
    return 0


# ------------------------------------------------------------------------------------------ report


def template(msg):
    msg = re.sub(r"'[^']*'", "_", msg)
    msg = re.sub(r"\b\d+\b", "N", msg)
    return msg.strip()


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def fmt(x, pct=False, digits=2):
    if x is None:
        return "-"
    return f"{x * 100:.0f}%" if pct else f"{x:.{digits}f}"


def report(rows):
    lines = []
    by_cond = collections.defaultdict(list)
    for r in rows:
        by_cond[(r["backend"], r["model"], r["skill"], r.get("effort"), r.get("e7"))].append(r)
    lines.append("| Backend | Model | Skill | E7 | Runs | Pass | Hidden tests | First compile OK | Compile failures / run "
                 "| Turns | Cost / run | Seconds |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for (backend, model, skill, effort, ver), rs in sorted(by_cond.items(), key=lambda kv: str(kv[0])):
        hidden = sum(r["hidden_passed"] for r in rs) / max(1, sum(r["hidden_total"] for r in rs))
        lines.append(
            f"| {backend} | {model}{'/' + effort if effort else ''} | {skill} | {ver or '-'} | {len(rs)} "
            f"| {fmt(mean([1.0 if r['passed'] else 0.0 for r in rs]), True)} | {fmt(hidden, True)} "
            f"| {fmt(mean([None if r.get('first_compile_ok') is None else float(r['first_compile_ok']) for r in rs]), True)} "
            f"| {fmt(mean([r.get('compile_failures') for r in rs]))} | {fmt(mean([r.get('turns') for r in rs]), digits=1)} "
            f"| ${fmt(mean([r.get('cost_usd') for r in rs]), digits=3)} | {fmt(mean([r.get('seconds') for r in rs]), digits=0)} |")
    lines.append("")
    groups = sorted({r["group"] for r in rows})
    lines.append("| Condition | " + " | ".join(groups) + " |")
    lines.append("|---|" + "---|" * len(groups))
    for key, rs in sorted(by_cond.items(), key=lambda kv: str(kv[0])):
        cells = []
        for g in groups:
            gr = [r for r in rs if r["group"] == g]
            cells.append(f"{fmt(mean([1.0 if r['passed'] else 0.0 for r in gr]), True)} ({len(gr)})" if gr else "-")
        lines.append(f"| {key[1]} skill {key[2]} | " + " | ".join(cells) + " |")
    lines.append("")
    failed = collections.Counter(r["task"] for r in rows if not r["passed"])
    if failed:
        lines.append("Tasks failed most often: " + ", ".join(f"{t} ({n})" for t, n in failed.most_common(12)))
        lines.append("")
    errs = collections.Counter(template(m) for r in rows for m in r.get("errors") or [])
    if errs:
        lines.append("| Compile error (names replaced by _) | Count |")
        lines.append("|---|---|")
        for m, n in errs.most_common(25):
            lines.append(f"| {m.replace('|', '\\|')} | {n} |")
    return "\n".join(lines)


def cmd_report(args):
    rows = []
    for f in args.files:
        for line in Path(f).read_text().splitlines():
            if line.strip():
                rows.append(json.loads(line))
    print(report(rows))
    return 0


# -------------------------------------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "build-rel"), help="directory with e7 and e7c")
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("validate", help="check every task's reference, starter and tests")
    v.add_argument("--tasks", help="comma-separated id prefixes (w01,p)")
    r = sub.add_parser("run", help="run a model or an agent on the tasks")
    r.add_argument("--backend", choices=["claude-code", "anthropic-api", "reference"], default="claude-code")
    r.add_argument("--model", default="sonnet", help="opus | sonnet | haiku, or a full model id")
    r.add_argument("--skill", choices=["on", "off"], default="off")
    r.add_argument("--effort", help="anthropic-api: output_config.effort (low ... max)")
    r.add_argument("--runs", type=int, default=1)
    r.add_argument("--jobs", type=int, default=4)
    r.add_argument("--tasks", help="comma-separated id prefixes (w01,p)")
    r.add_argument("--rounds", type=int, default=8, help="anthropic-api: model rounds per task")
    r.add_argument("--max-turns", type=int, default=60, help="claude-code: agent turns per task")
    r.add_argument("--timeout", type=int, default=1200, help="claude-code: seconds per task")
    r.add_argument("--out", help="results file (default: bench/ai/results/<stamp>-....jsonl)")
    pr = sub.add_parser("prepare", help="set up working directories for agents run by another tool")
    pr.add_argument("--tasks")
    pr.add_argument("--runs", type=int, default=1)
    pr.add_argument("--skill", choices=["on", "off"], default="off")
    pr.add_argument("--label", default="run")
    pr.add_argument("--dir", help="directory to create (default: bench/ai/results/<stamp>-external-<label>)")
    co = sub.add_parser("collect", help="grade a prepared directory after the agents finished")
    co.add_argument("dir")
    co.add_argument("--model", required=True, help="the model the agents used (for the report)")
    p = sub.add_parser("report", help="summarize results files")
    p.add_argument("files", nargs="+")
    args = ap.parse_args()
    return {"validate": cmd_validate, "run": cmd_run, "report": cmd_report, "prepare": cmd_prepare,
            "collect": cmd_collect}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
