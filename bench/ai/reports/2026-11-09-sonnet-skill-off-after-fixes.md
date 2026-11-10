# After the first fixes: Claude Sonnet 5.5, no skill (2026-11-09)

The baseline run ([2026-11-09-sonnet-skill-off.md](2026-11-09-sonnet-skill-off.md)) repeated with the compiler
changed by the fixes listed under *Unreleased* in the CHANGELOG (one compile reports all unsupported syntax,
`(e as Error)`, loop narrowing, catchable decoder errors, and others). Same 40 tasks, same prompts, same model,
one run per task; raw rows in `2026-11-09-sonnet-skill-off-after-fixes.jsonl`.

| | Pass | First compile OK | Compile failures, all tasks | Tool calls / task | Seconds / task |
|---|---|---|---|---|---|
| Before | 39/40 | 68% | 65 | 6.9 | 51 |
| After | 39/40 | 70% | 50 | 5.9 | 41 |

| Group | Compile failures / task, before → after | Agent code lines / reference lines, before → after |
|---|---|---|
| port | 5.7 → 2.3 | 2.66 → 1.66 |
| write | 0.33 → 1.53 | 1.82 → 1.63 |
| extend | 0.20 → 0.30 | 1.00 → 0.97 |
| fix | 0.20 → 0.20 | 1.00 → 1.00 |

- **Porting got much cheaper.** Every port task needed fewer compile rounds (p04: 12 → 1, p09: 11 → 3). The
  agent now sees all the unsupported constructs of a file in its first compile and fixes them together; the
  `(e as Error)` errors (21 before) are gone. Port solutions are about 40% shorter.
- **The write group rose only because of one run:** the w02 agent (no documentation) guessed `e7:crypto` names
  one compile at a time (23 compile failures, `hmacSha256`, `zzz`, ...). That error now lists the module's
  exports; without w02 the write group had 0 compile failures.
- **The failure is a different task each time** (w01 before, w11 now: an empty variable treated as missing),
  both model mistakes on edge cases the prompt states.
- One run per task is noisy; the direction is clear for porting, not for the small changes elsewhere.

Still visible without documentation: agents write their own JSON readers because `JSON.parse(text) as T` aborts
on a shape mismatch and they do not know `tryParse` (p02–p09), and their own CRC-32 / UTF-8 / SHA code (w02, w06,
w08) when they do not know `e7:encoding` / `e7:crypto`. That is what the skill and a short guide are for: the next
run is the same model with the skill.
