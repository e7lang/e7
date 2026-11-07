#!/usr/bin/env python3
"""Writes an update manifest for E7ModuleManager (Java/Kotlin, Swift) and signs it.

    tools/manifest.py --module app.e7b --version 2026.11.1 --url app-2026.11.1.e7b \\
        --rollout 10 --sign keys/release.seed -o manifest.json
    -> manifest.json and manifest.json.sig (hex Ed25519 detached signature, `e7c --sign-file`)

    tools/manifest.py --kill-switch --sign keys/release.seed -o manifest.json
    -> a manifest that makes every client fall back to the module bundled with the app

Manifest fields (see docs/DEPLOYMENT.md):
  version         module version label (shown in error reports; also salts the rollout bucket)
  url             module URL, absolute or relative to the manifest's URL
  sha256          hex SHA-256 of the module file (the signed .e7b as served)
  rolloutPercent  0..100: clients whose bucket (SHA-256 of "<version>:<userId>") is below it take the module
  killSwitch      true: clients drop downloaded modules and run the bundled fallback
  minVm           oldest e7vm that can run the module ("0.1.0"); read from the module header by default
  serial          increasing number; clients reject manifests older than the newest they accepted
                  (default: the current Unix time), so an old manifest cannot be replayed to undo a kill switch

Only the Python standard library is used; signing runs `e7c --sign-file` (pass --e7c if it is not on PATH).
"""
import argparse, hashlib, json, os, shutil, struct, subprocess, sys, time

MAGIC = b"E7B\0"
FORMAT = 3  # the e7b format identifier (docs/BYTECODE.md)
SIG_TRAILER = 76  # "E7S1" | key id (8) | Ed25519 signature (64)


def module_header(data):
    """(format, requires, signed) of an .e7b file, or None if it is not one."""
    if len(data) < 24 or data[:4] != MAGIC:
        return None
    fmt, _flags, requires = struct.unpack_from("<III", data, 4)
    signed = len(data) >= SIG_TRAILER and data[-SIG_TRAILER:-SIG_TRAILER + 4] == b"E7S1"
    return fmt, requires, signed


def version_text(v):
    return "%d.%d.%d" % (v >> 16, (v >> 8) & 0xFF, v & 0xFF)


def parse_version(s):
    s = s[1:] if s.startswith("v") else s
    parts = s.split(".")
    if len(parts) not in (2, 3) or not all(p.isdigit() and int(p) < 256 for p in parts):
        raise ValueError("invalid version %r (expected e.g. 1.2 or 1.2.3)" % s)
    a, b, c = (list(map(int, parts)) + [0])[:3]
    return "%d.%d.%d" % (a, b, c)


def find_e7c(given):
    if given:
        return given
    found = shutil.which("e7c")
    if found:
        return found
    here = os.path.dirname(os.path.abspath(__file__))
    for cand in ("build/e7c", "build/Release/e7c.exe", "build/e7c.exe"):
        p = os.path.join(here, "..", cand)
        if os.path.exists(p):
            return p
    sys.exit("manifest.py: e7c not found (pass --e7c path/to/e7c)")


def main():
    ap = argparse.ArgumentParser(description="Write (and sign) an E7 update manifest.")
    ap.add_argument("--module", help="the signed .e7b module to publish")
    ap.add_argument("--version", help="module version label, e.g. 2026.11.1")
    ap.add_argument("--url", help="module URL (absolute, or relative to the manifest URL)")
    ap.add_argument("--rollout", type=float, default=100, help="rollout percentage 0..100 (default 100)")
    ap.add_argument("--kill-switch", action="store_true", help="write a kill-switch manifest (no module)")
    ap.add_argument("--min-vm", help="oldest e7vm that can run the module (default: from the module header)")
    ap.add_argument("--serial", type=int, help="manifest serial number (default: current Unix time)")
    ap.add_argument("--allow-unsigned", action="store_true", help="accept a module without a signature trailer")
    ap.add_argument("-o", "--output", default="manifest.json", help="output file (default manifest.json)")
    ap.add_argument("--sign", metavar="SEED_FILE", help="sign with this key (e7c --keygen); writes <output>.sig")
    ap.add_argument("--e7c", help="path to e7c (default: PATH, then build/e7c)")
    a = ap.parse_args()

    serial = a.serial if a.serial is not None else int(time.time())
    if a.kill_switch:
        manifest = {"killSwitch": True, "serial": serial}
    else:
        if not (a.module and a.version and a.url):
            ap.error("--module, --version and --url are required (or --kill-switch)")
        if not 0 <= a.rollout <= 100:
            ap.error("--rollout must be between 0 and 100")
        with open(a.module, "rb") as f:
            data = f.read()
        hdr = module_header(data)
        if hdr is None:
            sys.exit("manifest.py: %s is not an e7b module" % a.module)
        fmt, requires, signed = hdr
        if fmt != FORMAT:
            sys.exit("manifest.py: %s has bytecode format %d; e7vm reads format %d (recompile it)" % (a.module, fmt, FORMAT))
        if not signed and not a.allow_unsigned:
            sys.exit("manifest.py: %s is not signed (e7c --sign <seed>); clients only load signed modules" % a.module)
        try:
            min_vm = parse_version(a.min_vm) if a.min_vm else version_text(requires)
        except ValueError as e:
            ap.error(str(e))
        rollout = int(a.rollout) if float(a.rollout).is_integer() else a.rollout
        manifest = {
            "version": a.version,
            "url": a.url,
            "sha256": hashlib.sha256(data).hexdigest(),
            "rolloutPercent": rollout,
            "killSwitch": False,
            "minVm": min_vm,
            "serial": serial,
        }
    text = json.dumps(manifest, indent=2) + "\n"
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s" % a.output)
    if a.sign:
        e7c = find_e7c(a.e7c)
        r = subprocess.run([e7c, "--sign-file", a.output, "--sign", a.sign])
        if r.returncode != 0:
            sys.exit("manifest.py: signing failed")
        print("wrote %s.sig" % a.output)
    else:
        print("not signed: run  e7c --sign-file %s --sign <seed-file>  before publishing" % a.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
