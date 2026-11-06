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

## Verifying releases

Every file attached to a [release](https://github.com/e7lang/e7/releases) (the `e7-<version>-<platform>`
archives, the Homebrew formula and Scoop manifest, `e7-skill.zip`, the SBOM) and every language package
(`.nupkg`, `.aar`, `.jar`, npm `.tgz`, `.vsix`) has a GitHub build provenance attestation (SLSA provenance
signed through Sigstore with the identity of the release workflow). Verify a download with the
[GitHub CLI](https://cli.github.com/):

```bash
gh attestation verify e7-0.2.0-linux-x64.tar.gz --repo e7lang/e7
gh attestation verify E7VM.0.2.0.nupkg --repo e7lang/e7
```

The command fails unless the file was built by a workflow of `e7lang/e7`; its output names the workflow
run and commit. The JavaScript package (`e7lang-e7-<version>.tgz`, attached to the release, not published to the npm
registry) and the VS Code extension are attested the same way. Each release
includes an SPDX software bill of materials, `e7-<version>.spdx.json`; the C dependencies are vendored
in `third_party/` (BearSSL, Monocypher).

## Supported versions

Security fixes are made for the latest release.
