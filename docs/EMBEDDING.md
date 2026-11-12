# Embedding guide: C API and Android / iOS / Windows integration

## 1. Recommended deployment flow (scripts delivered from the server)

```
Development: write .ts in VS Code (types from lib/e7.d.ts) → run locally with e7 / compare against Node
Build server: e7c app.ts -o app.e7b --sign release.seed      (the private key stays on the server)
Delivery: CDN / API response returns app.e7b (optionally wrapped in TLS)
App: e7_config.trusted_keys = release.pub → e7_load() checks the signature + verifies bytecode → e7_call()
```

For apps that receive modules over the air, [DEPLOYMENT.md](DEPLOYMENT.md) describes the complete flow: key
management and rotation, signed manifests, staged rollout, kill switch, rollback, the minimum VM version of a
module, sizing limits from measurements, error reporting and a shadow-run phase. `E7ModuleManager` (Java/Kotlin,
Swift and C#) implements the client side.

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
- `.e7b` carries its bytecode format and the oldest e7vm that can run it (`requires`); a VM rejects a module it
  cannot run with `E7_ERR_VERSION`. `e7_inspect` / `e7c --info` read both without loading the module (see
  [BYTECODE.md](BYTECODE.md#5-versions-and-compatibility)).
- Source can also be compiled on the device (`e7_compile` / `e7_load_source`, requires `E7_WITH_COMPILER=ON`);
  if you only need to run `.e7b`, use `-DE7_WITH_COMPILER=OFF` to reduce size.

## 2. C API overview (`include/e7/e7.h`)

| Function | Description |
|---|---|
| `e7_config_init` / `e7_vm_new` / `e7_vm_free` | Create/destroy a VM. Config: `memory_limit`, `stack_slots`, `max_frames`, `suspend_on_budget`, `require_signature`, `trusted_keys`, `gc_growth_percent`. Always fill the struct with `e7_config_init` first: it sets `struct_size`, which lets newer libraries add fields |
| `e7_load(vm, data, len, &module)` | Verify signature → parse → verify → link native functions → run module top-level code (`E7_ERR_VERSION` for a module of another bytecode format or one that needs a newer e7vm) |
| `e7_inspect(data, len, &info)` | Read a module's format, required e7vm version (`requires_version`) and signature key id without loading it |
| `e7_version` / `e7_version_number` | Library version (`"0.1.5"` / `0x000105`) |
| `e7_gc_stats_get(vm, &stats)` | Garbage collections, total and longest pause, heap size and peak |
| `e7_compile` / `e7_load_source` | Compile source (errors are formatted as `file:line:col: error: ...`) |
| `e7_compile_ex(src, len, name, &opts, ...)` | Compile with options (`e7_compile_options_init` first): `E7_COMPILE_DEBUG` (debug information), a `read_file` callback for imports |
| `e7_call(vm, module, "fn", args, n, &ret)` | Call an `export function` |
| `e7_set_budget(vm, ticks)` | Instruction budget for subsequent execution (≤0 means unlimited) |
| `e7_resume` / `e7_abort_suspended` | Resume / abandon a suspended call, or one waiting for async host functions (`E7_PENDING`) |
| `e7_register_native(vm, name, sig, fn, user)` | Register a host function (must be done before `e7_load`) |
| `e7_register_async_native` / `e7_settle` / `e7_settle_error` / `e7_open_tickets` | Async host functions: the script gets a promise, the host settles its ticket later ([§2.6](#26-async-host-functions)) |
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
`E7_ERR_VERSION` (bytecode format not supported, or the module requires a newer e7vm: recompile it with the
compiler that matches the app's VM), `E7_PAUSED` (only with a debugger attached in pause-to-host mode), and
`E7_PENDING` (the call waits for async host functions: [§2.6](#26-async-host-functions)).
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
(settled after the call returns), so the budget also caps the total time a script can spend in heavy native functions.
A native cannot be suspended halfway: with suspension enabled, one that does more work than the rest of the slice (such
as `pbkdf2` with many iterations) runs past the end of the slice and the call suspends right after it, but only by up to
2^22 ticks (about 4 million PBKDF2 iterations) or the slice, whichever is larger; a native that needs more fails with
`E7_ERR_BUDGET`. While a call is suspended, no new `e7_call` can be started (`E7_ERR_STATE`);
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

### 2.6 Async host functions

A script can `await` work the host does asynchronously (a network request, a Keystore / Keychain
operation that shows UI, a database query). The VM has no event loop and no threads of its own: **the host is the
event loop** ([DESIGN.md D2](DESIGN.md#d2-async--await)).

```ts
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";
declare function fetchKey(id: string): Promise<Uint8Array>;   // an async host function

export async function sign(id: string, body: Uint8Array): Promise<string> {
  const key = await fetchKey(id);
  return hexEncode(hmac("sha256", key, body));
}
```

The protocol of the C API:

1. `e7_register_async_native(vm, "fetchKey", "s>y", fn, user)` before `e7_load`. The signature is written as for
   `e7_register_native`, with the **promise's value type** as the return code (`"s>y"`: `(string) => Promise<Uint8Array>`;
   `v` for `Promise<void>`).
2. When the script calls it, the VM creates a pending promise and calls `fn(vm, args, nargs, ticket, user)`. `fn` starts
   the operation and returns `E7_OK`; returning an error status (with `e7_native_error`) throws in the script at the
   call instead, and no ticket stays open.
3. The host settles the ticket **on the VM's thread**, inside `fn` (at once) or later: `e7_settle(vm, ticket, &value)`
   fulfills the promise (`E7_ERR_ARGS`: a value of the wrong type, the ticket stays open), `e7_settle_error(vm, ticket, message)`
   rejects it with an `Error`. Unknown or already settled tickets give `E7_ERR_STATE`. `e7_open_tickets(vm)` counts the
   open ones.
4. `e7_call` runs until the script has nothing left to do. With tickets still open it returns **`E7_PENDING`**: settle
   one or more tickets, then `e7_resume(vm, &ret)`, which runs the promise jobs those settlements queued and returns
   `E7_OK` with the result, an error, or `E7_PENDING` again.
5. An `export async function f(): Promise<T>` is called like any export and the host gets `T`: the call waits for
   the promise (`E7_ERR_RUNTIME` with the error if it rejects, as does an unhandled rejection). Without open tickets
   (e.g. every async native settled at once) it returns `E7_OK` directly.
6. Top-level `await` of a host operation makes `e7_load` return `E7_PENDING`, with the module pointer already set;
   `e7_resume` finishes the initializer.
7. While a call is pending, `e7_call` fails with `E7_ERR_STATE` (as for a suspended call). `e7_abort_suspended`
   abandons it; settling its tickets later returns `E7_ERR_STATE`, so late completions are simply ignored.

Operations that complete on other threads hand their results to the VM's thread, which settles them **in the order
they complete** and resumes after each (one macrotask per completion, as Node.js runs `setImmediate` callbacks):

```c
static e7_status fetch_key(e7_vm* vm, const e7_value* a, size_t n, int64_t ticket, void* user) {
  start_fetch(a[0].v.str.ptr, a[0].v.str.len, ticket);   // its completion is pushed to `done_queue` by another thread
  return E7_OK;
}
e7_register_async_native(vm, "fetchKey", "s>y", fetch_key, NULL);
/* ... e7_load ... */
e7_status st = e7_call(vm, m, "sign", args, 2, &ret);
while (st == E7_PENDING) {
  struct completion c = queue_pop_blocking(&done_queue);  // the next operation that finished
  if (c.ok) {
    e7_value v = {.type = E7_BYTES};
    v.v.bytes.ptr = c.data;
    v.v.bytes.len = c.len;
    e7_settle(vm, c.ticket, &v);
  } else {
    e7_settle_error(vm, c.ticket, c.message);
  }
  st = e7_resume(vm, &ret);
}
```

`tests/capi_test.cpp` (last block) exercises the whole protocol.

**The language bindings drive this loop themselves.** Each registers async host functions with the language's own
async type and offers an async call that settles completions on the VM's thread in completion order. The synchronous
`call` / `load` / `resume` stay as they were; when they would return `E7_PENDING` they abandon the call and report the
pending status as an error (nothing else could settle its tickets).

- **Java / Kotlin**: register with `registerAsyncNative(name, sig, args -> CompletionStage<?>)`; call with
  `callAsync(executor, m, fn, args...)` → `CompletableFuture<Object>` or the blocking `callAwait(m, fn, args...)`; load
  (top-level `await`) with `loadAsync(executor, bytes)` / `loadAwait(bytes)`. A synchronous call that would wait throws
  `E7Exception` with status `PENDING`.
- **Swift**: `registerAsyncNative(name, signature:) { args async throws in ... }`; `try await callAsync(m, fn, args...)`;
  `try await loadAsync(bytes)`. A synchronous call that would wait throws `E7Error` with `isPending`.
- **C#**: `RegisterAsyncNative(name, sig, Func<object?[], Task<object?>>)` (or with a `CancellationToken`);
  `await CallAsync(m, fn, args...)` (an overload takes a `CancellationToken`) or the blocking `CallAwait(m, fn, args...)`;
  `await LoadAsync(bytes)` or the blocking `LoadAwait(bytes)`. A synchronous call that would wait throws `E7Exception`
  with `E7Status.Pending`.
- **JavaScript**: no host functions; `run(..., { call })` returns an async export's value and top-level `await` works in
  `run` (the status `"pending"` cannot occur).

Common rules: a host operation that fails rejects the script's promise with an `Error` carrying the exception's
message (Swift: the error's description); a value of the wrong type rejects it too, naming the function. Java and C#
settle an operation whose future / task is **already complete** at once, so a synchronous `call` works when all of
them complete immediately; Swift starts each operation in a new `Task`, so async natives need `callAsync`. While an
async call waits, other calls fail with `E7_ERR_STATE`. Cancelling the call (cancelling the Java future, the Swift
task or the C# token) or calling `abortSuspended()` abandons it; late completions are ignored. An async call does not
stop at an exhausted budget with `suspend_on_budget`: it is abandoned with the `SUSPENDED` status. `openTickets`
mirrors `e7_open_tickets`.

**Java.** The VM runs on an executor, the "VM thread", which must run tasks one at a time in order:
`Executors.newSingleThreadExecutor()`, Android's main thread (`context.getMainExecutor()`) or `handler::post`. Use the
VM only from that executor while a call is in progress. `callAwait` blocks the calling thread instead, which becomes
the VM thread (interrupting it abandons the call). On Android, `registerAsyncNative` and `callAsync` / `loadAsync`
need API 24 (`CompletionStage` / `CompletableFuture`); the rest of the class, including `callAwait` / `loadAwait`
(which `E7ModuleManager` uses for module initializers and `selfTest`), works from API 21.

```java
ExecutorService vmThread = Executors.newSingleThreadExecutor();
vm.registerAsyncNative("fetchKey", "s>y", args -> keyStore.fetchAsync((String) args[0]));  // CompletableFuture<byte[]>
E7VM.Module m = vm.loadAwait(bytecode);
CompletableFuture<Object> sig = vm.callAsync(vmThread, m, "sign", "k1", body);
sig.thenAccept(s -> show((String) s));
```

**Kotlin** wraps it in a `suspend` function with kotlinx-coroutines (cancelling the coroutine cancels the future,
which abandons the call):

```kotlin
import kotlinx.coroutines.future.await
import kotlinx.coroutines.future.future

val vmThread = Executors.newSingleThreadExecutor()
val ioScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

vm.registerAsyncNative("fetchKey", "s>y") { args -> ioScope.future { keyStore.fetch(args[0] as String) } }

suspend fun E7VM.callSuspend(module: E7VM.Module, function: String, vararg args: Any?): Any? =
  callAsync(vmThread, module, function, *args).await()

val sig = vm.callSuspend(module, "sign", "k1", body) as String
```

**Swift.** `callAsync` runs the VM on the caller's actor (Swift 6 compilers: an `isolation` parameter defaulting to
`#isolation`, e.g. the main actor); host operations run in their own tasks and their results come back through an
`AsyncStream`. Cancelling the task that awaits `callAsync` abandons the call and cancels the host tasks.

```swift
try vm.registerAsyncNative("fetchKey", signature: "s>y") { args in
  guard case .string(let id) = args[0] else { throw KeyError.badId }
  return .bytes(try await keychain.fetch(id))
}
let module = try await vm.loadAsync(bytecode)
let sig = try await vm.callAsync(module, "sign", .string("k1"), .bytes(body))
```

**C#.** `CallAsync` starts on the calling thread and settles each completion where its `await` continues: the
caller's `SynchronizationContext` (e.g. the UI thread), otherwise one thread-pool thread at a time.

```csharp
vm.RegisterAsyncNative("fetchKey", "s>y", async args => await keyStore.FetchAsync((string)args[0]!));
var module = await vm.LoadAsync(bytecode);
var sig = (string)(await vm.CallAsync(module, "sign", "k1", body))!;
```

**JavaScript.** The npm package has no host functions, so nothing can stay pending: `run` completes async exports
and top-level `await` (and `e7:test`'s `later()` stand-ins) before it returns; `result` is the promise's value.

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
    implementation("org.e7vm:e7vm-android:0.1.5")   // Android
    // implementation("org.e7vm:e7vm:0.1.5")         // desktop / server JVM
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
a downloaded AAR can be used with `implementation(files("libs/e7vm-android-0.1.5.aar"))`.

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
Async host functions: `registerAsyncNative`, `callAsync` / `callAwait`, `loadAsync` / `loadAwait` ([§2.6](#26-async-host-functions)).

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
`platform/java/test/E7VMTest.java` and `ModuleManagerTest.java` additionally run without JUnit in `ci.yml`,
as do the async host function scenarios (`AsyncScenarios.java`, JUnit `E7VMAsyncTest`, plain `AsyncTest.java`).

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
  .package(url: "https://github.com/e7lang/e7.git", from: "0.1.5"),
],
targets: [
  .target(name: "App", dependencies: [.product(name: "E7VM", package: "e7")]),
]
```

Then `import E7VM`. The C API is also available as `import CE7VM` (e.g. for the `E7_ERR_*` status codes).

**XCFramework (manual integration).** `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework` (CMake + Xcode;
iOS 15.0 by default, `IOS_DEPLOYMENT_TARGET=13.0` with an Xcode that still supports it);
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
Async host functions: `registerAsyncNative(_:signature:_:)` with an `async` closure, `callAsync` / `loadAsync`
([§2.6](#26-async-host-functions)).
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
  dotnet add package E7VM --version 0.1.5
  ```

  The package targets `netstandard2.0` and `net8.0` (managed assembly `E7.dll`, namespace `E7`). It is also attached to each GitHub release and published to
  GitHub Packages (`https://nuget.pkg.github.com/e7lang/index.json`).
  Without NuGet, add `platform/windows/E7VM.cs` (and `E7ModuleManager.cs` for over-the-air updates) to your project and ship `e7vm.dll` (or `libe7vm.so` / `libe7vm.dylib`)
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

Async host functions: `RegisterAsyncNative` with a `Task`-returning function, `CallAsync` / `LoadAsync`, and the
blocking `CallAwait` / `LoadAwait` ([§2.6](#26-async-host-functions)). `E7ModuleManager` handles over-the-air
delivery ([DEPLOYMENT.md](DEPLOYMENT.md#4-client-integration)).

## 6. JavaScript / WebAssembly (Node.js, browsers, editors)

The JavaScript package [`@e7lang/e7`](../packages/e7) is the compiler and VM built with Emscripten
(`tools/e7wasm.cpp`). It is not published to the npm registry: install `e7lang-e7-<version>.tgz` from a GitHub
release (`npm install ./e7lang-e7-0.1.5.tgz`). Every `run` uses a fresh VM with an instruction budget and a memory limit; host
functions and `KeyHandle` are not available from JavaScript (use the native API for those). Async exports and
top-level `await` complete within `run` ([§2.6](#26-async-host-functions)).

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

- Android arm64-v8a, stripped, all crypto algorithms included, libc++ linked statically (NDK r28): `libe7vm_jni.so`, the library in the AAR, is about **1.0 MB** for the runtime only (`MinSizeRel`; 1.1 MB `Release`) and about **2.7 MB** with the on-device compiler (3.0 MB `Release`). Compressed (as in an APK) it is about 40% of that. The JNI binding is about 100 KB of it: the C API library alone (`libe7vm.so`, `-DE7_BUILD_SHARED=ON`) is about 930 KB for the runtime only. Of the runtime, the standard library is about 310 KB, the VM about 170 KB, core (bytecode loader, verifier, number formatting) about 75 KB, the parts of libc++ it uses about 80 KB, dynamic-linking and unwind tables about 130 KB, and all of `e7:crypto` (BearSSL, Monocypher and the natives) about 46 KB. An app that receives compiled `.e7b` modules from a server needs only the runtime (`-DE7_WITH_COMPILER=OFF`, or `platform/android/build_android.sh --no-compiler`); the AAR in the release includes the compiler.
- iOS arm64 static library: what it adds to a stripped app that calls the whole C API (the linker drops unused code, and libc++ comes with the OS) is about **610 KB** for the runtime only (`MinSizeRel`; 720 KB `Release`) and about **1.5 MB** with the on-device compiler (1.9 MB `Release`). `platform/ios/build_xcframework.sh --no-compiler` builds the runtime-only XCFramework.
- Interpreter performance is comparable to or faster than V8's interpreter (`--jitless`); standard crypto algorithms run as native code. See the benchmark table in the README.
