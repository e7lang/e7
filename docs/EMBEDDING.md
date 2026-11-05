# Embedding guide: C API and Android / iOS / Windows integration

## 1. Recommended deployment flow (scripts delivered from the server)

```
Development: write .ts in VS Code (types from lib/e7.d.ts) → run locally with e7 / compare against Node
Build server: e7c app.ts -o app.e7b --sign release.seed      (the private key stays on the server)
Delivery: CDN / API response returns app.e7b (optionally wrapped in TLS)
App: e7_config.trusted_keys = release.pub → e7_load() checks the signature + verifies bytecode → e7_call()
```

For apps that receive modules over the air, [DEPLOYMENT.md](DEPLOYMENT.md) describes the complete flow: key
management and rotation, signed manifests, staged rollout, kill switch, rollback, `e7c --target` per client VM
version, sizing limits from measurements, error reporting and a shadow-run phase. `E7ModuleManager` (Java/Kotlin
and Swift) implements the client side.

- **Signing is optional.** A plain `e7c app.ts -o app.e7b` module runs anywhere. Signing only adds a
  guarantee that the module came from you, which matters when it is downloaded at runtime. The rules:

  | Receiver configuration | Unsigned module | Signed module |
  |---|---|---|
  | default (no trusted keys) | runs | runs (signature ignored) |
  | `trusted_keys` set | runs | runs only if the signature verifies against a trusted key |
  | `require_signature = 1` + `trusted_keys` | rejected | runs only if the signature verifies against a trusted key |

  On the command line, `e7 --trust release.pub app.e7b` is the equivalent of the last row.
- Generate keys: `e7c --keygen release` produces `release.seed` (32-byte private key seed, hex, **keep secret**) and `release.pub` (public key, built into the app).
- Multiple public keys are supported (key rotation): pass N×32 bytes in `trusted_keys`.
- `.e7b` carries a format version and the minimum e7vm version it needs; a VM rejects a module it cannot run with
  `E7_ERR_VERSION`. Build with `e7c --target <version>` for the oldest app version you still serve, or keep one
  build per VM version and pick it with `e7_inspect` / `e7c --info` (see [BYTECODE.md](BYTECODE.md#5-compatibility)).
- Source can also be compiled on the device (`e7_compile` / `e7_load_source`, requires `E7_WITH_COMPILER=ON`);
  if you only need to run `.e7b`, use `-DE7_WITH_COMPILER=OFF` to reduce size.

## 2. C API overview (`include/e7/e7.h`)

| Function | Description |
|---|---|
| `e7_config_init` / `e7_vm_new` / `e7_vm_free` | Create/destroy a VM. Config: `memory_limit`, `stack_slots`, `max_frames`, `suspend_on_budget`, `require_signature`, `trusted_keys`, `gc_growth_percent`. Always fill the struct with `e7_config_init` first: it sets `struct_size`, which lets newer libraries add fields |
| `e7_load(vm, data, len, &module)` | Verify signature → parse → verify → link native functions → run module top-level code (`E7_ERR_VERSION` for a module of another bytecode format or one that needs a newer e7vm) |
| `e7_inspect(data, len, &info)` | Read a module's format, required e7vm version (`requires_version`) and signature key id without loading it |
| `e7_version` / `e7_version_number` | Library version (`"0.2.5"` / `0x000205`) |
| `e7_gc_stats_get(vm, &stats)` | Garbage collections, total and longest pause, heap size and peak |
| `e7_compile` / `e7_load_source` | Compile source (errors are formatted as `file:line:col: error: ...`) |
| `e7_compile_ex(src, len, name, &opts, ...)` | Compile with options (`e7_compile_options_init` first): `E7_COMPILE_DEBUG` (debug information), a `read_file` callback for imports, `target_version` (oldest e7vm that must run the module) |
| `e7_call(vm, module, "fn", args, n, &ret)` | Call an `export function` |
| `e7_set_budget(vm, ticks)` | Instruction budget for subsequent execution (≤0 means unlimited) |
| `e7_resume` / `e7_abort_suspended` | Resume / abandon a suspended call |
| `e7_register_native(vm, name, sig, fn, user)` | Register a host function (must be done before `e7_load`) |
| `e7_set_key_provider` | Provide keys for `KeyHandle` |
| `e7_set_print` | Take over `console.log` output |
| `e7_seed_random` | Fix the `Math.random` seed |
| `e7_sign_module` / `e7_public_key` | Ed25519 signing tools (usually used on the server) |
| `e7_sign_detached` / `e7_verify_detached` | Detached Ed25519 signatures of other files, e.g. update manifests (`e7c --sign-file` / `--verify-file`; [DEPLOYMENT.md](DEPLOYMENT.md#3-manifests)) |
| `e7_last_error` | Most recent error message (including the script call stack) |
| `e7_debug_*` | Debugger: breakpoints, stepping, pause, call stack and variables ([§2.4](#24-debugging)) |

**Value type** `e7_value`: `E7_NUMBER`(double), `E7_BOOL`, `E7_INT`/`E7_UINT`(64-bit), `E7_STRING`(UTF-8 pointer+length),
`E7_BYTES`, `E7_HANDLE`, `E7_VOID`, `E7_NULL`. Returned strings/bytes are owned by the VM and are **valid until the next call into that VM**.

**Signature codes** (host functions and exported functions): `n` number, `b` boolean, `s` string, `y` Uint8Array, `h` KeyHandle, `v` void,
`1..8` = `i8 u8 i16 u16 i32 u32 i64 u64`. Example: `"sy>s"` means `(string, Uint8Array) => string`.

**Error codes**: `E7_OK`, `E7_SUSPENDED`, `E7_ERR_RUNTIME` (script error/throw/abort), `E7_ERR_BUDGET`, `E7_ERR_MEMORY`,
`E7_ERR_STACK`, `E7_ERR_LOAD`, `E7_ERR_VERIFY`, `E7_ERR_SIGNATURE`, `E7_ERR_NOT_FOUND`, `E7_ERR_ARGS`, `E7_ERR_COMPILE`, `E7_ERR_STATE`,
`E7_ERR_VERSION` (bytecode format or required e7vm version not supported: deliver a module built with
`e7c --target <app's e7vm version>`), and `E7_PAUSED` (only with a debugger attached in pause-to-host mode).
The VM remains usable after any error; module globals are preserved.

### 2.1 Host functions

```ts
// Script side
declare function deviceId(): string;
declare function hostLog(level: number, msg: string): void;
```
```c
static e7_status device_id(e7_vm* vm, const e7_value* a, size_t n, e7_value* ret, void* user) {
  ret->type = E7_STRING; ret->v.str.ptr = g_device_id; ret->v.str.len = strlen(g_device_id);
  return E7_OK;            // On error: e7_native_error(vm, "msg"); return E7_ERR_RUNTIME;
}
e7_register_native(vm, "deviceId", ">s", device_id, NULL);
```
When a module is loaded, the script's declarations are checked against the signatures registered by the host; a missing or mismatched function makes `e7_load` return `E7_ERR_LOAD`.
Scripts can call **only** these explicitly registered functions (plus the built-in standard library); they have no file, network, or system access.

### 2.2 KeyHandle (keys never enter the script)

```ts
import type { KeyHandle } from "e7:crypto";
export function sign(k: KeyHandle, data: string): string { /* hmac("sha256", k, ...) */ }
```
```c
static int key_lookup(void* user, int64_t id, const uint8_t** key, size_t* len) {
  /* Decrypt the key from Keychain / Keystore / DPAPI into a host-managed buffer */
  *key = buf; *len = buf_len; return 0;     // Return non-zero for an unknown id
}
e7_set_key_provider(vm, key_lookup, NULL);
e7_value args[2] = {{.type = E7_HANDLE, .v.handle = 1}, ...};
```
The script only receives an unforgeable handle object; `hmac` uses the host-provided bytes directly in native code, so the script cannot read, print, or exfiltrate the key.

### 2.3 Instruction budget and time-sliced execution

```c
e7_config cfg; e7_config_init(&cfg); cfg.suspend_on_budget = 1;
e7_vm* vm = e7_vm_new(&cfg);
e7_set_budget(vm, 200000);                       // Roughly 1 ms per slice
e7_status st = e7_call(vm, m, "longTask", args, n, &ret);
while (st == E7_SUSPENDED) {                     // e.g. resume one slice per frame so the UI stays responsive
  e7_set_budget(vm, 200000);
  st = e7_resume(vm, &ret);
}
```
Without suspension enabled, an exhausted budget returns `E7_ERR_BUDGET`, which guards against infinite loops. Native functions for strings/arrays/encoding/JSON/hashing/BigInt etc. charge the budget by the amount of data processed
(settled after the call returns), so the budget also caps the total time a script can spend in heavy native functions. While a call is suspended, no new `e7_call` can be started (`E7_ERR_STATE`);
use `e7_abort_suspended` to abandon it. `e7_ticks_used(vm)` returns the ticks charged since the VM was
created (with any budget, also unlimited): a deterministic, machine-independent measure of the work a script
did, useful for sizing budgets (`e7 --stats` prints it).

### 2.4 Debugging

A host can debug the scripts it runs (an in-app developer console, a remote debugger, tests). Compile with debug
information, attach a debugger before loading, and handle stops either in a callback or by getting `E7_PAUSED`
back from `e7_load` / `e7_call`:

```c
static e7_debug_action on_stop(e7_vm* vm, const e7_debug_stop* st, void* user) {
  fprintf(stderr, "stopped at %s:%u:%u in %s\n", st->where.file, st->where.line, st->where.column, st->where.function);
  const e7_debug_scope* scopes;
  size_t n = e7_debug_scopes(vm, 0, &scopes);                 // Locals, Closure, Globals of frame 0
  for (size_t i = 0; i < n; i++) {
    const e7_debug_variable* v;
    size_t nv = e7_debug_variables(vm, scopes[i].ref, 0, 0, &v);
    for (size_t k = 0; k < nv; k++) fprintf(stderr, "  %s: %s = %s\n", v[k].name, v[k].type, v[k].value);
  }
  return E7_DEBUG_STEP_OVER;                                   // or block here until the user decides
}

e7_compile_options opts;
e7_compile_options_init(&opts);
opts.flags = E7_COMPILE_DEBUG;
e7_compile_ex(src, len, "main.ts", &opts, &bc, &bc_len, &errs);
e7_debug_attach(vm, on_stop, NULL);                           // NULL callback: pause-to-host (E7_PAUSED)
e7_debug_set_breakpoint(vm, "main.ts", 12);
e7_load(vm, bc, bc_len, &mod);
```

`e7_debug_pause` can be called from another thread while a script runs. Without an attached debugger the VM
runs its normal interpreter and the debugger costs nothing; with debug information, runtime error traces also
show columns (`at f (main.ts:12:5)`). See [DEBUGGING.md](DEBUGGING.md) for the execution models, stepping
semantics, the inspection API and the `e7 debug` command-line debugger.

### 2.5 Threads

A VM may be used from only one thread; different VMs share no state and can run in parallel on different threads. For concurrency, create one VM per thread
(each can load the same `.e7b`).

## 3. Android and JVM (Java / Kotlin)

### 3.1 Gradle dependency (recommended)

The binding is published as two Maven artifacts (group `org.e7vm`):

| Artifact | Contents |
|---|---|
| `org.e7vm:e7vm-android` (AAR) | Java API + `libe7vm_jni.so` for `arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86` (minSdk 21, 16 KB page aligned, compiler included), consumer R8/ProGuard rules |
| `org.e7vm:e7vm` (JAR) | Java API (Java 8+) + JNI libraries for Linux x86_64/aarch64, macOS (universal) and Windows x86_64 |

```kotlin
// app/build.gradle.kts
dependencies {
    implementation("org.e7vm:e7vm-android:0.2.5")   // Android
    // implementation("org.e7vm:e7vm:0.2.5")         // desktop / server JVM
}
```

Until the artifacts are on Maven Central, use GitHub Packages. It requires a GitHub token with the
`read:packages` scope, even for public packages:

```kotlin
// settings.gradle.kts
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven("https://maven.pkg.github.com/e7lang/e7") {
            credentials {
                username = providers.gradleProperty("gpr.user").orNull ?: System.getenv("GITHUB_ACTOR")
                password = providers.gradleProperty("gpr.key").orNull ?: System.getenv("GITHUB_TOKEN")
            }
        }
    }
}
```

The `.aar` and `.jar` are also attached to each [GitHub release](https://github.com/e7lang/e7/releases);
a downloaded AAR can be used with `implementation(files("libs/e7vm-android-0.2.5.aar"))`.

On the desktop JVM, `org.e7vm.NativeLoader` loads the library in this order: the path in the system
property `e7vm.library`, then `System.loadLibrary("e7vm_jni")` (`java.library.path`), then the copy bundled
in the jar (extracted to a temporary file). On other platforms, build it yourself with
`cmake -S . -B build -DE7_BUILD_JNI=ON && cmake --build build --target e7vm_jni`.

### 3.2 Usage

```kotlin
import org.e7vm.*

val vm = E7VM(E7VM.Config().requireSignature(SERVER_PUBLIC_KEY).memoryLimit(32L shl 20))
vm.setKeyProvider { id -> keyStoreDecrypt(id) }          // Returns a ByteArray
vm.registerNative("deviceId", ">s") { _ -> Settings.Secure.getString(cr, ANDROID_ID) }
val module = vm.load(bytesFromServer)                      // Throws E7Exception if signature check/verification fails
vm.setBudget(10_000_000)
val sig = vm.call(module, "sign", "a=1&b=2", KeyHandle(1)) as String
vm.close()
```
Arguments: `Double/Float` → number, `Int/Long` → integer, `Boolean`, `String`, `ByteArray` → Uint8Array, `KeyHandle`.
Returns: `Double`, `Long`, `Boolean`, `String`, `ByteArray`, or `null`. Errors are `E7Exception(status, message)`.
`E7VM.Config().gcGrowthPercent(n)`, `vm.gcStats()`, `E7VM.versionNumber()`, `E7VM.inspect(bytes)` and
`E7VM.signDetached` / `verifyDetached` mirror the C functions of the same names. For over-the-air delivery use
`E7ModuleManager` (signed manifest, rollout, kill switch, rollback; see [DEPLOYMENT.md](DEPLOYMENT.md#4-client-integration)).

### 3.3 Building from source

- Gradle (what is published): `cd platform/android && ./gradlew :library:assembleRelease` builds the AAR with
  the NDK through the repository's `CMakeLists.txt` (set `E7_NDK_PATH` to pick an NDK, `-Pe7.abis=x86_64` to
  limit ABIs). `./gradlew -Pe7.jvmOnly=true :jvm:build` builds the jar without the Android SDK.
- Without Gradle: `ANDROID_NDK_HOME=... platform/android/build_android.sh` builds `build-android/jniLibs/<abi>/libe7vm_jni.so`;
  copy it into `app/src/main/jniLibs/` and add `platform/java/org/e7vm/*.java` to the project.

### 3.4 Tests and publishing

[`.github/workflows/maven.yml`](../.github/workflows/maven.yml) runs the JUnit suite
`platform/android/shared-test` on desktop JVMs (Linux x64/arm64, macOS, Windows) against the bundled
libraries, and as an instrumented test on an Android emulator (API 30, x86_64) against the AAR. It checks the
AAR (all four ABIs, consumer rules, 16 KB alignment) and the jar, and uploads the AAR as the `e7-android-aar`
build artifact. The suite includes the `E7ModuleManager` scenarios (`ModuleManagerScenarios.java`), which
`platform/java/test/E7VMTest.java` and `ModuleManagerTest.java` additionally run without JUnit in `ci.yml`.

Releases call it with `publish: true`: artifacts go to GitHub Packages, and to Maven Central (Central Portal
bundle via `scripts/maven-central-bundle.sh`) when the repository has the secrets `MAVEN_CENTRAL_USERNAME`,
`MAVEN_CENTRAL_PASSWORD` (a Central Portal user token for the verified `org.e7vm` namespace), `SIGNING_KEY`
(ASCII-armored PGP private key) and `SIGNING_PASSWORD`; otherwise the Central step is skipped with a notice.

## 4. iOS (Swift)

**Swift Package Manager (recommended, iOS 13+ / macOS 11+).** The package builds the VM, the compiler and the
crypto libraries from source; no prebuilt binary is involved. In Xcode use *File → Add Package Dependencies…* with
`https://github.com/e7lang/e7.git`, or in `Package.swift`:

```swift
dependencies: [
  .package(url: "https://github.com/e7lang/e7.git", from: "0.2.5"),
],
targets: [
  .target(name: "App", dependencies: [.product(name: "E7VM", package: "e7")]),
]
```

Then `import E7VM`. The C API is also available as `import CE7VM` (e.g. for the `E7_ERR_*` status codes).

**XCFramework (manual integration).** `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework` (CMake + Xcode);
add it together with `platform/ios/E7VM/E7VM.swift` (and `E7ModuleManager.swift` for over-the-air updates) to the app target.

```swift
import E7VM

let vm = try E7VM(memoryLimit: 32 << 20, trustedKeys: [serverPublicKey])
vm.keyProvider = { id in Keychain.key(for: id) }
try vm.registerNative("deviceId", signature: ">s") { _ in .string(UIDevice.current.identifierForVendor!.uuidString) }
let module = try vm.load(bytecode)
vm.setBudget(10_000_000)
if case let .string(sig) = try vm.call(module, "sign", .string("a=1&b=2"), .handle(KeyHandle(1))) { print(sig) }
```

Values are `E7Value` cases: `.number(Double)`, `.int(Int64)`, `.uint(UInt64)`, `.bool`, `.string`, `.bytes(Data)`,
`.handle(KeyHandle)` and `.void`. Errors are `E7Error(status, message)` (`isSuspended` for time-slicing with `resume()`).
`E7VM.compile(_:)`, `E7VM.sign(_:seed:)` and `E7VM.publicKey(seed:)` compile and sign modules (e.g. in tests or tooling).
`E7VM(gcGrowthPercent:)`, `vm.gcStats()`, `E7VM.versionNumber`, `E7VM.inspect(_:)` and `E7VM.signDetached` /
`verifyDetached` mirror the C API; `E7ModuleManager` handles over-the-air delivery ([DEPLOYMENT.md](DEPLOYMENT.md#4-client-integration)).
The wrapper is tested by `Tests/E7VMTests` (`swift test` on macOS and on an iOS simulator; `scripts/macos.sh test`).

e7vm uses no JIT and allocates no executable memory, which meets the App Store requirements for interpreted code (delivered scripts should not change the app's primary purpose).

## 5. Windows (C / C++ / C#)

- C/C++: `platform/windows/build_windows.ps1` (VS 2022; `-ClangCl` enables computed goto dispatch, `-Shared` produces `e7vm.dll`),
  link `e7vm_static.lib` (plus `e7_bearssl.lib`, `e7_monocypher.lib`, `advapi32.lib`) or `e7vm.dll`.
- C# / .NET: install the [`E7VM`](https://www.nuget.org/packages/E7VM) NuGet package. It contains the binding
  (`platform/windows/E7VM.cs`, P/Invoke, 64-bit processes) and the native library for Windows x64, Linux x64 / arm64
  and macOS 11+ (universal), which .NET loads automatically from `runtimes/<rid>/native/`:

  ```bash
  dotnet add package E7VM --version 0.2.5
  ```

  The package targets `netstandard2.0` and `net8.0` (managed assembly `E7.dll`, namespace `E7`). It is also attached to each GitHub release and published to
  GitHub Packages (`https://nuget.pkg.github.com/e7lang/index.json`).
  Without NuGet, add `platform/windows/E7VM.cs` to your project and ship `e7vm.dll` (or `libe7vm.so` / `libe7vm.dylib`)
  next to your app; build it with `-Shared` or `cmake -DE7_BUILD_SHARED=ON`.
  Tested in CI by `platform/windows/test`, both against a local build
  (`dotnet run --project platform/windows/test -- <dir with the library>`) and from the packed `.nupkg` on
  Linux (x64, arm64), Windows and macOS (`.github/workflows/nuget.yml`).

```csharp
using var vm = new E7.E7VM(memoryLimit: 32 << 20, suspendOnBudget: false, serverPublicKey);
vm.SetKeyProvider(id => ProtectedData.Unprotect(LoadBlob(id), null, DataProtectionScope.CurrentUser));
var module = vm.Load(File.ReadAllBytes("app.e7b"));
vm.SetBudget(10_000_000);
var sig = (string)vm.Call(module, "sign", "a=1&b=2", new E7.KeyHandle(1))!;
```

## 6. JavaScript / WebAssembly (Node.js, browsers, editors)

The npm package [`@e7lang/e7`](../packages/e7) is the compiler and VM built with Emscripten
(`tools/e7wasm.cpp`). Every `run` uses a fresh VM with an instruction budget and a memory limit; host
functions and `KeyHandle` are not available from JavaScript (use the native API for those).

```js
import { compile, run } from "@e7lang/e7";
const { bytecode, diagnostics } = await compile(source, { fileName: "sign.ts" });
const r = await run(bytecode, { call: "sign", args: ["a=1&b=2", "secret"], budget: 10_000_000, memoryLimit: 32 << 20 });
if (r.ok) console.log(r.result); else console.error(r.status, r.error);
```

`.e7b` files are portable: bytecode produced by `compile()` loads in the native VM (`e7_load`) and vice
versa. The WebAssembly module also exports a debugger session API (`e7w_dbg_*`, JSON events) for debug
adapters running the VM in a worker; see [DEBUGGING.md §7](DEBUGGING.md#7-webassembly--javascript-the-json-protocol). The `e7-js` command (`run`, `check`, `compile -o`, `disasm`) covers the `e7` / `e7c` workflow
without a native build, and the VS Code extension ([editors/vscode](../editors/vscode)) uses the same
package for diagnostics and *Run File*.

## 7. Security recommendations

1. **When scripts are downloaded at runtime, enable signature checking** (`require_signature` + built-in public key); keep the private key only on the build server or an HSM.
   Sign the update manifest too (detached signature), so that rollouts and the kill switch cannot be forged; see [DEPLOYMENT.md](DEPLOYMENT.md).
2. Set `memory_limit` (e.g. 16–64 MiB) and an instruction budget; for scripts driven by external input, set a smaller `max_frames`.
3. Provide keys through `KeyHandle`; do not pass them to scripts as string/byte arguments.
4. Register only the host functions you need, and validate their arguments on the host side.
5. The bytecode verifier + runtime checks ensure tampered bytecode cannot corrupt memory; however, "the algorithm logic itself" cannot be kept fully secret on the client,
   so keys that must stay secret should remain in the host or on the server.
6. Script error messages include the source file name and line number; to hide them, compile on the server with different file names.

## 8. Size and performance reference

- Linux x86-64 shared library (`MinSizeRel`, stripped, all crypto algorithms included): about **470 KB** for the runtime only, about **920 KB** with the on-device compiler.
- Interpreter performance is comparable to or faster than V8's interpreter (`--jitless`); standard crypto algorithms run as native code. See the benchmark table in the README.
