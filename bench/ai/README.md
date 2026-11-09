# AI benchmark

How well do AI models and coding agents write E7? This benchmark gives a model a task, lets it compile and test
its code, and then grades the result with hidden tests. Every change meant to make E7 easier for AI (better
diagnostics, fewer rejected TypeScript forms, a shorter guide) is measured against it.

## Tasks

`tasks/` holds 40 tasks in four groups:

| Prefix | Group | What the model gets | What it must do |
|---|---|---|---|
| `w` | write | a description of a script | write `solution.ts` from scratch |
| `p` | port | `original.ts`, ordinary TypeScript written without E7 in mind | write `solution.ts`, the same behaviour in E7 |
| `e` | extend | a working `solution.ts` | add a feature |
| `f` | fix | a `solution.ts` with a bug and a bug report | fix it |

A task is a directory:

```
tasks/w01-webhook-verify/
  task.json            {"id", "group", "title", "file": "solution.ts"}
  prompt.md            the request, as a user would write it; it states the exported API exactly
  starter/             files the model starts with (copied into its working directory)
    solution.test.ts   visible example tests (the model may run and change them)
    original.ts        port tasks: the TypeScript to port
    solution.ts        extend / fix tasks: the code to change
  hidden.test.ts       the grading tests (never shown to the model)
  reference/
    solution.ts        a reference solution (must pass both test files)
```

Rules for a task:

- **Solvable in E7.** `run.py validate` checks that the reference passes the visible and the hidden tests. For
  fix tasks it also checks that the starter fails the hidden tests; for extend tasks, that the starter compiles.
- **The API is exact.** The prompt names every exported function and interface the tests import, with E7-legal
  types. Error cases have exact messages or result strings, so tests can check them.
- **Deterministic.** No clocks or randomness: time is a parameter.
- **Natural.** Prompts read like real requests (pricing rules, signatures, parsers, state machines, data
  transforms), not like puzzles. Port tasks use the TypeScript habits people really have (`Record`, unions,
  `for...in`, regular expressions where a string method would do, classes with getters...), but only where an E7
  rewrite exists.
- **Graded by behaviour.** Hidden tests check results, including edge cases the prompt states, not style.

## Running

```bash
python3 bench/ai/run.py validate                       # every reference passes; checks the starters
python3 bench/ai/run.py run --model sonnet --skill off --runs 3 --jobs 4
python3 bench/ai/run.py report bench/ai/results/*.jsonl
```

`run` drives a coding agent (`--backend claude-code`, the default: the `claude` CLI, logged in with
`claude auth login`) or a plain model loop (`--backend anthropic-api`: `pip install anthropic` and an API key).
`--backend reference` grades the reference solutions through the same path, to check the harness for free. The agent works in a temporary directory
with the task's starter files and `e7` / `e7c` on its `PATH`; `--skill on` also loads the E7 skill
(`skills/e7`). After the agent stops, its `solution.ts` is graded with the hidden tests.

Agents started by another tool (subagents of a coding assistant, another IDE agent) use `prepare` and `collect`:

```bash
python3 bench/ai/run.py prepare --skill off --label sonnet --dir /tmp/run1   # work dirs, prompts, logging e7/e7c
# give each agent the prompt in /tmp/run1/prompts/<task>-0.md; it works in /tmp/run1/work/<task>-0
python3 bench/ai/run.py collect /tmp/run1 --model sonnet                     # grade, write /tmp/run1.jsonl
```

`prepare` gives every task its own `e7` / `e7c` wrappers that log each call and its output, so `collect` counts
compile failures from what the agent really ran. A `usage.json` in the directory (`{"<task>-0": {"tokens",
"turns", "seconds"}}`) adds what the agents cost.

The plain model loop sends the prompt and the starter files, takes the `solution.ts` code block from the
answer, compiles it and runs the visible tests, and sends errors back, for at most `--rounds` rounds (default 8).

Each run is one JSON line in the results file:

| Field | Meaning |
|---|---|
| `passed` | every hidden test passed |
| `hidden_passed`, `hidden_total` | hidden tests passed / total |
| `first_compile_ok` | the first version of `solution.ts` compiled |
| `compile_failures` | how many times the compiler rejected the model's code |
| `rounds` / `turns` | model rounds (API loop) or agent turns (claude-code) |
| `input_tokens`, `output_tokens`, `cost_usd`, `seconds` | what the run cost |
| `errors` | every compile error message the model saw, in order |

`report` prints, per model and skill setting: pass rate, first-compile rate, compile failures and cost per task,
results per group, and the most frequent compile errors (messages with names replaced by `_`). The error list
is the to-do list for diagnostics and for TypeScript forms E7 should accept.

`e7` / `e7c` come from `--bin` (default: `build-rel`). Results go to `bench/ai/results/` (not committed);
reports worth keeping go to `bench/ai/reports/`.
