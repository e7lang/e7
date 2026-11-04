# Security policy

e7vm is designed to run bytecode that may be hostile, so security reports are very welcome.

## Reporting a vulnerability

Please **do not open a public issue**. Report it privately through
[GitHub private vulnerability reporting](https://github.com/e7lang/e7/security/advisories/new)
(Security → Advisories → Report a vulnerability). Include the E7 version (`e7 --version`), the platform,
and a script or `.e7b` file that reproduces the problem if you have one.

You will get an answer within a few days. Fixes ship with a regression test and are noted in the release notes.

## Scope

In scope: memory-safety or sandbox escapes from bytecode or source (verifier, interpreter, GC, natives),
limit bypasses (instruction budget, memory, call depth), signature-check bypasses, and incorrect results
from `e7:crypto`. See [docs/PRODUCTION.md](docs/PRODUCTION.md) for the threat model and past review findings.

## Supported versions

Security fixes are made for the latest release.
