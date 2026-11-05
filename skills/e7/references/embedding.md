# Embedding: how apps run E7 scripts

Read this when designing a script's exported API or writing host code.

## The lifecycle

1. The script is compiled on a build server: `e7c sign.ts -o sign.e7b` (optionally `--sign release.seed`).
2. The app downloads or bundles `sign.e7b`.
3. The app creates a VM with limits, registers host functions and a key provider, loads the bytecode
   (it is verified first; top-level code runs once), sets an instruction budget and calls exported functions.

## Designing the exported API

Exported functions of the entry file are the script's API. Their parameters and results can only be:

| E7 type | C (`e7_type`) | Kotlin/Java | Swift `E7Value` | C# | JavaScript (`@e7lang/e7`) |
|---|---|---|---|---|---|
| `number` | `E7_NUMBER` | `Double` | `.number` | `double` | `number` |
| `boolean` | `E7_BOOL` | `Boolean` | `.bool` | `bool` | `boolean` |
| `i8`..`i64` | `E7_INT` | `Long` | `.int` | `long` | `bigint` |
| `u8`..`u64` | `E7_UINT` | `Long` | `.uint` | `ulong` | `bigint` |
| `string` | `E7_STRING` | `String` | `.string` | `string` | `string` |
| `Uint8Array` | `E7_BYTES` | `ByteArray` | `.bytes` | `byte[]` | `Uint8Array` |
| `KeyHandle` | `E7_HANDLE` | `KeyHandle` | `.handle` | `KeyHandle` | — |
| `void` (result only) | `E7_VOID` | `null` | `.void` | `null` | `undefined` |

Guidelines:

- Keep the API small and stable: the app is released less often than the script.
- Pass structured input as a JSON string and parse it with `JSON.parse(text) as Input`; return structured
  output with `JSON.stringify(result)`.
- Never take secrets as `string`/`Uint8Array` arguments when the app can keep them: take a `KeyHandle` and pass it
  to `hmac`, `ecdsaSign`, `rsaSign`, ... The script can use the key but never read it.
- Report expected failures in the result (e.g. `{"ok":false,"error":"..."}`); reserve `throw` for broken invariants.

## Host functions

Scripts call into the app through functions the app registers. Declare them with their signature:

```ts fragment
declare function deviceId(): string;
declare function nowMillis(): number;
declare function storeGet(key: string): string;

export function fingerprint(): string {
  return `${deviceId()}:${Math.floor(nowMillis() / 1000)}`;
}
```

The VM checks at load time that the app registered each declared function with the same signature, so a
script fails to load (instead of misbehaving) if it needs something the app doesn't provide.

## Limits

The host sets a memory limit, a call-depth limit, and an instruction budget per call (heavy native calls such
as PBKDF2 count too). Exceeding one stops the call with an error. Avoid unbounded loops over input sizes the
script does not control; prefer native helpers (`hash`, `hmac`, `base64Encode`) over byte-by-byte loops.

## Host-side snippets

```kotlin
// Android (org.e7vm:e7vm-android)
val vm = E7VM(E7VM.Config().memoryLimit(32L shl 20))
vm.setKeyProvider { id -> keyStoreDecrypt(id) }
val module = vm.load(bytecode)
vm.setBudget(10_000_000)
val sig = vm.call(module, "sign", "a=1&b=2", KeyHandle(1)) as String
```

```swift
// iOS / macOS (Swift package https://github.com/e7lang/e7, product E7VM)
let vm = try E7VM(memoryLimit: 32 << 20)
vm.keyProvider = { id in Keychain.key(for: id) }
let module = try vm.load(bytecode)
vm.setBudget(10_000_000)
let sig = try vm.call(module, "sign", .string("a=1&b=2"), .handle(KeyHandle(1)))
```

```csharp
// .NET (NuGet E7VM)
using var vm = new E7.E7VM(memoryLimit: 32 << 20);
var module = vm.Load(File.ReadAllBytes("sign.e7b"));
vm.SetBudget(10_000_000);
var sig = (string)vm.Call(module, "sign", "a=1&b=2", "s3cr3t")!;
```

```js
// JavaScript (npm @e7lang/e7, WebAssembly): compile and run without native tools
import { compile, run } from "@e7lang/e7";
const { ok, bytecode, diagnostics } = await compile(source, { fileName: "sign.ts" });
const result = await run(bytecode, { call: "sign", args: ["a=1&b=2", "s3cr3t"], budget: 1e7 });
```

Full C API and binding documentation: `docs/EMBEDDING.md` in github.com/e7lang/e7.
