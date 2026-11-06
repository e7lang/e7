# Deploying E7 modules to apps

This guide covers delivering E7 modules over the air to Android, JVM and iOS apps: keys, signing,
manifests, staged rollout, kill switch, rollback, choosing limits, error reporting, and a shadow-run
phase before the first rollout. The client side is `E7ModuleManager`
([Java / Kotlin](../platform/java/org/e7vm/E7ModuleManager.java),
[Swift](../platform/ios/E7VM/E7ModuleManager.swift)); the tools are `e7c` and
[`tools/manifest.py`](../tools/manifest.py). For the C API and platform setup see [EMBEDDING.md](EMBEDDING.md).

```
build server                                  CDN (static files)                app
------------                                  ------------------                ---
e7c app.ts --target 0.2 --sign release.seed  →  app-2026.11.1.e7b   (immutable)
tools/manifest.py ... --sign release.seed     →  manifest.json       (short TTL)  →  E7ModuleManager.update():
                                                 manifest.json.sig                     verify manifest signature
                                                                                       rollout bucket, kill switch, minVm
                                                                                       download, sha256, e7_inspect
                                                                                       e7_load (module signature), selfTest()
                                                                                       activate, keep as last-known-good
```

## 1. Keys

- `e7c --keygen release` writes `release.seed` (the 32-byte Ed25519 secret seed, hex) and `release.pub`
  (the public key, hex). The seed stays on the build server: a CI secret, or better a signing service or
  HSM that only the release pipeline can use. The public key is compiled into the app.
- The same trusted keys verify **modules** (the signature trailer that `e7c --sign` appends, checked by
  `e7_load`) and **manifests** (a detached signature, `e7_verify_detached`). A detached signature is made
  over `"e7-detached-v1\0" || file bytes`, so it can never be reused as a module signature or the other way
  round.
- `e7c --info app.e7b` prints the key id (first 8 bytes of SHA-256 of the public key) of a signed module.

**Rotation.** Apps accept any of several keys (`trustedKeys(current, next)` / `trustedKeys: [current, next]`,
or N × 32 bytes in `e7_config.trusted_keys`). Ship every app release with the current key and the next one.
To rotate: start signing with `next`; once the app versions that only know the old key have aged out, ship
`[next, next+1]` and retire the old seed. Keep the next seed generated and stored before you need it.

**Compromise.** A leaked seed lets an attacker sign modules and manifests, including a manifest with a very
large `serial` that makes clients ignore your later manifests. Ship an app update without the leaked key (and
with a fresh one) as soon as possible; the bundled fallback keeps working meanwhile. This is why the seed
belongs in an HSM or signing service and not on developer machines.

## 2. Building and signing modules

```sh
e7c pricing.ts --target 0.2 --sign keys/release.seed -o dist/pricing-2026.11.1.e7b
e7c --info dist/pricing-2026.11.1.e7b      # format 2, requires e7vm 0.2.0, signed: key id ..., exports
```

**One build per client VM line.** Every `.e7b` records the oldest e7vm that can run it (`requires`), computed
from the instructions and natives it uses. A VM refuses modules that need a newer VM (`E7_ERR_VERSION`,
`E7Exception.VERSION`) instead of failing in the verifier. `e7c --target <version>` makes the compiler
refuse features that the target VM does not have, so the module is guaranteed to run there:

| clients run | build with | publish under |
|---|---|---|
| e7vm 0.2.x | `e7c --target 0.2` | `…/vm-0.2/manifest.json` |
| e7vm 0.3.x (later) | `e7c --target 0.3` | `…/vm-0.3/manifest.json` |

Pick the manifest URL in the app from `E7VM.versionNumber()` (or keep one manifest and rely on `minVm`, if
all clients can run the same build). A module may also be published to newer VM lines unchanged: a 0.2 module
runs on later VMs that still read its bytecode format.

From JavaScript, `compile(source, { target: "0.2" })` and `inspect(bytecode)` (`@e7lang/e7`) do the same.

## 3. Manifests

`manifest.json` (served next to the modules; the signature is `manifest.json.sig`):

| field | type | meaning |
|---|---|---|
| `version` | string | module version label; appears in error reports and salts the rollout bucket |
| `url` | string | module URL, absolute or relative to the manifest URL |
| `sha256` | hex string | SHA-256 of the module file exactly as served (the signed `.e7b`) |
| `rolloutPercent` | number 0–100 | share of users that take this version (fractions allowed, e.g. `0.5`) |
| `killSwitch` | boolean | `true`: every client drops downloaded modules and runs the bundled fallback |
| `minVm` | string, optional | oldest e7vm that can run the module (`"0.2.0"`); clients below it skip without downloading |
| `serial` | integer, optional | increases with every manifest you publish; see below |

A kill-switch manifest needs only `{"killSwitch": true, "serial": N}`.

Produce and sign it with `tools/manifest.py` (standard-library Python; it runs `e7c --sign-file`):

```sh
tools/manifest.py --module dist/pricing-2026.11.1.e7b --version 2026.11.1 \
    --url pricing-2026.11.1.e7b --rollout 5 --sign keys/release.seed -o dist/manifest.json
# -> dist/manifest.json (sha256 and minVm from the module, serial = current Unix time)
#    dist/manifest.json.sig
e7c --verify-file dist/manifest.json --trust keys/release.pub    # exit code 0: signature ok
```

Without the script, write the JSON yourself and sign it with
`e7c --sign-file manifest.json --sign release.seed` (writes `manifest.json.sig`: 128 hex digits and a
newline). The clients also accept the raw 64-byte signature.

**Serial (replay protection).** A manifest is only valid if it carries a signature, but an old signed manifest
is still a valid signature. Once a client has accepted a manifest with a `serial`, it ignores manifests with a
smaller serial or none, so nobody can serve last week's manifest to undo a kill switch or a rollback.
`manifest.py` uses the current Unix time; any increasing number works.

**Hosting.** Module files are immutable (put the version or hash in the name) and can be cached forever; give
`manifest.json` and its `.sig` a short cache lifetime and upload the module first, then the signature and the
manifest. A client that fetches a new manifest with an old signature (or the other way round) reports a
signature error and keeps its current module until the next check; nothing breaks.

## 4. Client integration

Kotlin (Android: call `update()` from WorkManager or a background thread, never on the main thread):

```kotlin
val mm = E7ModuleManager.Builder()
    .manifestUrl("https://updates.example.com/pricing/vm-0.2/manifest.json")
    .trustedKeys(RELEASE_KEY, NEXT_RELEASE_KEY)
    .fetcher { url -> http.get(url).bytes() }               // your HTTP stack; no networking library is bundled
    .storageDir(File(context.filesDir, "e7/pricing"))
    .userId(installationId)                                 // stable; never a raw account id if you can avoid it
    .fallback(context.assets.open("pricing.e7b").readBytes(), "bundled-${BuildConfig.VERSION_NAME}")
    .vmConfig(E7VM.Config().memoryLimit(16L shl 20))
    .vmSetup { vm -> vm.setBudget(5_000_000); vm.registerNative("now", ">n") { System.currentTimeMillis().toDouble() } }
    .errorReporter { version, status, message -> telemetry.e7Error(version, status, message) }
    .maxFailures(3)
    .build()

mm.start()                      // app start: last-known-good from disk (no network), or the fallback
executor.execute { mm.update() } // later: check the manifest and switch modules if needed

try {
    val price = mm.active().call("price", orderJson) as Double
    mm.reportSuccess()
} catch (e: E7Exception) {
    mm.reportFailure(e.status, e.message ?: "")
    // use the native implementation for this request
}
```

Swift:

```swift
var cfg = E7ModuleManager.Configuration(
  manifestURL: URL(string: "https://updates.example.com/pricing/vm-0.2/manifest.json")!,
  trustedKeys: [releaseKey, nextReleaseKey],
  storageDirectory: appSupport.appendingPathComponent("e7/pricing"),
  userId: installationId,
  fallbackModule: try Data(contentsOf: Bundle.main.url(forResource: "pricing", withExtension: "e7b")!),
  fallbackVersion: "bundled-\(appVersion)")
cfg.memoryLimit = 16 << 20
cfg.setUpVM = { vm in vm.setBudget(5_000_000) }
cfg.reportError = { version, status, message in Telemetry.e7Error(version, status, message) }
let mm = try E7ModuleManager(configuration: cfg, fetcher: { url in try HTTP.syncGet(url) })
try mm.start()
DispatchQueue.global(qos: .utility).async { _ = try? mm.update() }
```

What `update()` does, in order (any failure leaves the current module active and goes to the error reporter):

1. Fetch `manifest.json` and `manifest.json.sig`; verify the signature against the trusted keys.
2. Reject stale manifests (`serial`). Apply the kill switch.
3. Rollout: skip if this user's bucket is outside `rolloutPercent` (not an error).
4. Skip if `minVm` is newer than this VM, or if this version was rolled back before.
5. Download (or reuse the stored copy), check `sha256`, `e7_inspect` (format and required VM version).
6. Load into a fresh VM with the trusted keys (module signature, verifier), run `vmSetup` first.
7. If the module exports `selfTest()`, it must return `true`.
8. Store it as last-known-good (the previous one is kept for rollback), close the old VM, activate.

The manager owns its VMs: a replaced VM is closed. Use the module through `active()` each time, and do not run
`update()` while another thread is inside a call of the same manager's module.

Async exports ([EMBEDDING.md §2.6](EMBEDDING.md#26-async-host-functions)): call them with
`active().callAsync(vmExecutor, "quote", order)` / `callAwait("quote", order)` (Java, Kotlin) or
`try await mm.active!.callAsync("quote", .string(order))` (Swift). The Java manager also waits for async host
functions registered in `vmSetup` while it runs a module's top-level code and `selfTest()` (on the thread that calls
`start()` / `update()`); the Swift manager runs them synchronously, so a module whose top level or `selfTest()`
waits for a host operation is reported with `E7_PENDING` and not activated.

## 5. Staged rollout

A client takes a version when `bucket < rolloutPercent × 100`, where

```
bucket = uint32_be(SHA-256(UTF-8(version + ":" + userId))[0..4]) mod 10000
```

The bucket is stable for a given user and version, so raising the percentage only adds users (1 → 5 → 25 →
100 %), and it differs between versions, so the same users are not always first. The server can compute
the bucket to know who is in a stage. Suggested stages: internal users (a separate manifest URL, or user ids
you know are in the first bucket range), then 1 %, 5 %, 25 %, 100 %, each after a clean error-rate review.

Lowering `rolloutPercent` stops more clients from taking the version, but clients that already run it keep
it. To take a version back, publish a manifest for the previous version (with a higher `serial`; clients
switch to it), or use the kill switch.

## 6. Kill switch

```sh
tools/manifest.py --kill-switch --sign keys/release.seed -o dist/manifest.json
```

On the next `update()`, every client drops to the bundled fallback and remembers the state across restarts
(`start()` then activates the fallback without network). Downloaded modules stay on disk. Publishing a normal
manifest again (newer `serial`) lifts the kill switch; clients reuse the stored module without downloading it.
The fallback should be a complete, tested module (or a thin module that calls a native implementation via
host functions): it is what users run whenever anything goes wrong.

How fast the kill switch reaches users depends on how often the app calls `update()`: on start, on resume and
every few hours is typical.

## 7. Rollback

The host decides what counts as a failure and reports it with `reportFailure(status, message)`: a call that
throws (`E7Exception` / `E7Error`: runtime error, budget or memory limit), or a result your code rejects
(validation, a mismatch with the server). `reportSuccess()` resets the count. After `maxFailures` (default 3)
**consecutive** failures the manager reports a rollback, marks that module bad (it will not be activated
again, even if the manifest still names it), and activates the previous good module, or the fallback when there
is none. A module that no longer loads (e.g. after an app update with a newer VM that dropped its format) is
dropped the same way at `start()`.

## 8. Sizing the instruction budget and memory limit

Set both from measurements of the real module with representative (and worst-case) inputs, not guesses:

- **Memory.** After running the inputs, read the GC statistics: `vm.gcStats()` (Java), `vm.gcStats()` (Swift),
  `vm.GcStats()` (C#), `e7_gc_stats_get` (C): `peak_heap_bytes`, number of collections, total and longest pause.
  Set `memory_limit` to 2–4× the largest peak you observed. `gc_growth_percent` (default 200, minimum 125) trades
  memory for GC time: a lower value keeps the heap closer to the live size but collects more often; compare
  `peak_heap_bytes` and `total_pause_ns` at 150 and 200 for your workload.
- **Instructions.** Run the inputs with a series of budgets (`e7 app.e7b --call fn --budget N`, or
  `setBudget(N)` in a test) and find the smallest that passes; or run in suspend mode with a small slice
  (`e7 --slice N`, `suspend_on_budget`) and count the slices. Set the budget to about 10× the largest count
  and check the wall time of that many instructions on your slowest supported device (budget exhaustion is
  `E7_ERR_BUDGET`, reported like any other failure). A future `e7 --stats` will print these numbers directly.
- Repeat the measurement for each new module version during the shadow run (§10), with the limits you plan to
  ship, and watch `BUDGET` / `MEMORY` errors during the rollout.

## 9. Error reporting

The error reporter receives `(moduleVersion, status, message)`: `moduleVersion` is `null`/`nil` when the
manifest itself was the problem; `status` is an `e7_status` code:

| situation | status |
|---|---|
| manifest or module download failed, malformed manifest | `E7_ERR_LOAD` (6) |
| manifest signature invalid, stale manifest (serial), module sha256 mismatch | `E7_ERR_SIGNATURE` (8) |
| module needs a newer VM (`minVm`, module header) | `E7_ERR_VERSION` (14) |
| module rejected by `e7_load` (signature, verifier, natives) | the `e7_load` status |
| `selfTest()` returned something other than `true`, or threw | `E7_ERR_RUNTIME` (2) or its status |
| rollback after consecutive failures | the status passed to `reportFailure` |
| the manager cannot write its storage directory | `E7_ERR_STATE` (12) |

Send these with the app version, `E7VM.version()` and the platform to your telemetry, and alert per module
version: a burst of `SIGNATURE` errors means a broken publish (or an attack), `VERSION` errors mean a build for
the wrong `--target`, rollbacks mean a bad module. Do not include user data in messages you add yourself;
script error messages contain only the module's file names, line numbers and the script's own message text.

## 10. Shadow run before the first rollout

Before an E7 module replaces a native implementation, run both and compare, without letting the E7 result
affect users:

```kotlin
fun price(order: Order): Double {
    val native = nativePrice(order)                      // the result users get during the shadow phase
    if (shadowSampler.sample()) executor.execute {       // e.g. 1-10 % of calls, off the hot path
        try {
            val e7 = mm.active().call("price", order.toJson()) as Double
            if (!sameResult(native, e7)) telemetry.e7Mismatch(mm.active().version, order.redacted(), native, e7)
            else telemetry.e7Match(mm.active().version)
        } catch (e: E7Exception) {
            telemetry.e7Error(mm.active().version, e.status, e.message)
        }
    }
    return native
}
```

- Ship the module with a manifest at 100 % (or the stage you want to observe) while the app is in shadow mode;
  the switch between shadow mode and E7 results is a separate remote flag of your app, per module version.
- Compare normalized results (rounding, ordering, formatting of numbers) and count mismatches per version and
  input class; fix the module or the native code until mismatches are zero over a volume of calls that covers
  your input classes, and errors are at the level you accept.
- Record timing and the GC statistics (§8) in the same samples to set the budget and memory limit.
- Then switch the flag to use E7 results with the staged rollout of §5, keeping the native implementation as
  the fallback path for failures until the module has a clean history.

## 11. Checklist

- [ ] Release seed in an HSM / signing service; app ships with the current and the next public key.
- [ ] Modules built with `--target` for each client VM line and signed; `e7c --info` checked in CI.
- [ ] `manifest.json` + `.sig` produced by `tools/manifest.py`, verified with `e7c --verify-file`, increasing `serial`.
- [ ] A complete fallback module bundled in the app; `selfTest()` exported by every module.
- [ ] Budget and memory limit set from measurements; `reportFailure` / `reportSuccess` wired.
- [ ] Error reports with module version reach a dashboard with alerts.
- [ ] Shadow run finished without mismatches; rollout stages and the kill switch rehearsed on a test build.
