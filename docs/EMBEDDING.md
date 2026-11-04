# Embedding guide: C API and Android / iOS / Windows integration

## 1. Recommended deployment flow (scripts delivered from the server)

```
Development: write .ts in VS Code (types from lib/e7.d.ts) → run locally with e7 / compare against Node
Build server: e7c app.ts -o app.e7b --sign release.seed      (the private key stays on the server)
Delivery: CDN / API response returns app.e7b (optionally wrapped in TLS)
App: e7_config.trusted_keys = release.pub → e7_load() checks the signature + verifies bytecode → e7_call()
```

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
- `.e7b` carries a format version; the VM rejects versions it does not recognize. The server can deliver the matching build for each client VM version.
- Source can also be compiled on the device (`e7_compile` / `e7_load_source`, requires `E7_WITH_COMPILER=ON`);
  if you only need to run `.e7b`, use `-DE7_WITH_COMPILER=OFF` to reduce size.

## 2. C API overview (`include/e7/e7.h`)

| Function | Description |
|---|---|
| `e7_config_init` / `e7_vm_new` / `e7_vm_free` | Create/destroy a VM. Config: `memory_limit`, `stack_slots`, `max_frames`, `suspend_on_budget`, `require_signature`, `trusted_keys` |
| `e7_load(vm, data, len, &module)` | Verify signature → parse → verify → link native functions → run module top-level code |
| `e7_compile` / `e7_load_source` | Compile source (errors are formatted as `file:line:col: error: ...`) |
| `e7_call(vm, module, "fn", args, n, &ret)` | Call an `export function` |
| `e7_set_budget(vm, ticks)` | Instruction budget for subsequent execution (≤0 means unlimited) |
| `e7_resume` / `e7_abort_suspended` | Resume / abandon a suspended call |
| `e7_register_native(vm, name, sig, fn, user)` | Register a host function (must be done before `e7_load`) |
| `e7_set_key_provider` | Provide keys for `KeyHandle` |
| `e7_set_print` | Take over `console.log` output |
| `e7_seed_random` | Fix the `Math.random` seed |
| `e7_sign_module` / `e7_public_key` | Ed25519 signing tools (usually used on the server) |
| `e7_last_error` | Most recent error message (including the script call stack) |

**Value type** `e7_value`: `E7_NUMBER`(double), `E7_BOOL`, `E7_INT`/`E7_UINT`(64-bit), `E7_STRING`(UTF-8 pointer+length),
`E7_BYTES`, `E7_HANDLE`, `E7_VOID`, `E7_NULL`. Returned strings/bytes are owned by the VM and are **valid until the next call into that VM**.

**Signature codes** (host functions and exported functions): `n` number, `b` boolean, `s` string, `y` Uint8Array, `h` KeyHandle, `v` void,
`1..8` = `i8 u8 i16 u16 i32 u32 i64 u64`. Example: `"sy>s"` means `(string, Uint8Array) => string`.

**Error codes**: `E7_OK`, `E7_SUSPENDED`, `E7_ERR_RUNTIME` (script error/throw/abort), `E7_ERR_BUDGET`, `E7_ERR_MEMORY`,
`E7_ERR_STACK`, `E7_ERR_LOAD`, `E7_ERR_VERIFY`, `E7_ERR_SIGNATURE`, `E7_ERR_NOT_FOUND`, `E7_ERR_ARGS`, `E7_ERR_COMPILE`, `E7_ERR_STATE`.
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
use `e7_abort_suspended` to abandon it.

### 2.4 Threads

A VM may be used from only one thread; different VMs share no state and can run in parallel on different threads. For concurrency, create one VM per thread
(each can load the same `.e7b`).

## 3. Android (Java / Kotlin)

Build: `ANDROID_NDK_HOME=... platform/android/build_android.sh`, copy `build-android/jniLibs/*` into
`app/src/main/jniLibs/`, and add `platform/java/org/e7vm/*.java` to the project (usable directly from Kotlin).

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
The JNI binding is tested on a desktop JVM (OpenJDK 21) with `platform/java/test/E7VMTest.java`.

## 4. iOS (Swift)

Build: `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework`; add it together with `platform/ios/E7VM/E7VM.swift`
to the app target.

```swift
let vm = try E7VM(memoryLimit: 32 << 20, trustedKeys: [serverPublicKey])
vm.keyProvider = { id in Keychain.key(for: id) }
try vm.registerNative("deviceId", signature: ">s") { _ in .string(UIDevice.current.identifierForVendor!.uuidString) }
let module = try vm.load(bytecode)
vm.setBudget(10_000_000)
if case let .string(sig) = try vm.call(module, "sign", .string("a=1&b=2"), .handle(KeyHandle(1))) { print(sig) }
```
e7vm uses no JIT and allocates no executable memory, which meets the App Store requirements for interpreted code (delivered scripts should not change the app's primary purpose).

## 5. Windows (C / C++ / C#)

- C/C++: `platform/windows/build_windows.ps1` (VS 2022; `-ClangCl` enables computed goto dispatch, `-Shared` produces `e7vm.dll`),
  link `e7vm_static.lib` (plus `e7_bearssl.lib`, `e7_monocypher.lib`, `advapi32.lib`) or `e7vm.dll`.
- C#: add `platform/windows/E7VM.cs` to your project (P/Invoke, 64-bit; needs `e7vm.dll`, or `libe7vm.so` / `libe7vm.dylib` with .NET on Linux / macOS).
  Tested in CI by `platform/windows/test` (`dotnet run --project platform/windows/test -- <dir with the library>`).

```csharp
using var vm = new E7.E7VM(memoryLimit: 32 << 20, suspendOnBudget: false, serverPublicKey);
vm.SetKeyProvider(id => ProtectedData.Unprotect(LoadBlob(id), null, DataProtectionScope.CurrentUser));
var module = vm.Load(File.ReadAllBytes("app.e7b"));
vm.SetBudget(10_000_000);
var sig = (string)vm.Call(module, "sign", "a=1&b=2", new E7.KeyHandle(1))!;
```

## 6. Security recommendations

1. **When scripts are downloaded at runtime, enable signature checking** (`require_signature` + built-in public key); keep the private key only on the build server or an HSM.
2. Set `memory_limit` (e.g. 16–64 MiB) and an instruction budget; for scripts driven by external input, set a smaller `max_frames`.
3. Provide keys through `KeyHandle`; do not pass them to scripts as string/byte arguments.
4. Register only the host functions you need, and validate their arguments on the host side.
5. The bytecode verifier + runtime checks ensure tampered bytecode cannot corrupt memory; however, "the algorithm logic itself" cannot be kept fully secret on the client,
   so keys that must stay secret should remain in the host or on the server.
6. Script error messages include the source file name and line number; to hide them, compile on the server with different file names.

## 7. Size and performance reference

- Linux x86-64 shared library (`MinSizeRel`, stripped, all crypto algorithms included): about **470 KB** for the runtime only, about **920 KB** with the on-device compiler.
- Interpreter performance is comparable to or faster than V8's interpreter (`--jitless`); standard crypto algorithms run as native code. See the benchmark table in the README.
