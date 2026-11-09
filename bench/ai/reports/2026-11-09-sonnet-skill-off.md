# Baseline: Claude Sonnet 5.5, no skill (2026-11-09)

First run of the 40 tasks. Each task was done by a Claude Code subagent on Sonnet 5.5, with no E7 skill
and no documentation: the agent knew only the task prompt and that E7 is "a strict subset of TypeScript". It
could compile and run the tests. E7 0.1.3 (`build-rel`). Raw rows: `2026-11-09-sonnet-skill-off.jsonl`
(collected with `run.py prepare` / `collect`; turns are tool calls, tokens include the agent's fixed context of
about 54k).

| Model | Skill | Runs | Pass | Hidden tests | First compile OK | Compile failures / run | Tool calls | Seconds |
|---|---|---|---|---|---|---|---|---|
| sonnet | off | 40 | 98% | 100% | 68% | 1.62 | 6.8 | 51 |

| Group | Pass | Compile failures / run | Agent code lines / reference lines |
|---|---|---|---|
| write | 14/15 | 0.33 | 1.82 |
| extend | 10/10 | 0.20 | 1.00 |
| fix | 5/5 | 0.20 | 1.00 |
| port | 10/10 | 5.70 | 2.66 |

The one failure (w01) was an ambiguity in the prompt (is an empty header "malformed"?); the prompt now says so.

## What the numbers say

- **Correctness is not the problem.** Sonnet passes almost every hidden test without documentation. A pass rate
  near 100% cannot show improvements, so the next runs must use weaker models (Haiku) and more runs per task.
- **Porting is where E7 costs effort:** 5.7 compile failures per port task against 0.2–0.3 elsewhere, and
  port solutions are 2.7 times as long as the reference ports (p06: 1,024 lines against 130).
- **The agents did not know the standard library.** Without documentation they wrote their own JSON parsers
  (p02–p06, because `JSON.parse(text) as T` aborts on a shape mismatch and they did not know `tryParse`), their
  own SHA-256 / HMAC / base64url (w02: 415 lines against 52, not knowing `e7:crypto`), and their own date
  parsers (p06, p07, believing E7 has no `Date`). The code passes, but it is code nobody should ship.

## Compile errors the agents hit

| Error (names replaced by _) | Count | What it points to |
|---|---|---|
| only Error objects can be thrown; got _ | 38 | p06: a custom exception class (`class Fail`); `class X extends Error` is not supported either |
| conversion of type 'unknown' to type 'Error' is not supported | 21 | `catch (e) { (e as Error).message }`: the most common TS habit: 8 of 10 port tasks and 2 write tasks |
| JSON.parse must be used with a type assertion | 15 | |
| type _ is not supported (all types must be static) | 11 | `any` / `unknown` |
| utility type _ is not supported | 10 | `Record`, `Partial` |
| union types other than _ are not supported | 8 | |
| regular expressions are not supported | 8 | |
| Object.keys requires an object | 6 | |
| index signatures are not supported | 6 | |
| function _ lacks ending return statement | 4 | exhaustive switch over a union |
| for...in is not supported | 4 | |

Every message here already names the replacement, yet the agents needed several rounds for port tasks: one
error at a time (parse-level errors stop the compiler), and replacements that need knowledge the messages do
not give (`tryParse`, `e7:crypto`).

## Next

- Same run with the skill (does documentation shrink the port code and the compile failures?).
- Haiku with and without the skill, 3 runs per task (a model that fails often enough to measure).
- Candidates for E7 itself, by count: `(e as Error)` in a catch clause; throwing / catching error subclasses
  (`class X extends Error`); reporting every parse-level error in one compile; pointing to `tryParse` and
  `e7:crypto` from the errors that lead people to write their own.
