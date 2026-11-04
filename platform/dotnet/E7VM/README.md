# E7VM for .NET

C# binding for **e7vm**, the embeddable bytecode VM of [E7](https://github.com/e7lang/e7), a strict
TypeScript subset. Use it to run server-delivered (optionally signed) scripts inside your app with
memory limits, instruction budgets, host functions and key handles that keep secrets out of script memory.

The package ships the native library for **Windows x64, Linux x64, Linux arm64 and macOS 11+
(Apple Silicon and Intel)**; .NET picks the right one automatically. 64-bit processes only.

```bash
dotnet add package E7VM
```

```csharp
using E7;

byte[] code = E7VM.Compile(@"
export function greet(name: string): string { return ""hello, "" + name; }
", "greet.ts");                                     // or ship .e7b files built with the e7c compiler

using var vm = new E7VM(memoryLimit: 32 << 20);     // optional: trusted public keys for signed modules
var module = vm.Load(code);
vm.SetBudget(10_000_000);                           // instruction budget per call
Console.WriteLine((string?)vm.Call(module, "greet", "e7"));
```

Host functions, key providers, print handlers, suspend/resume and module signing are described in the
[embedding guide](https://github.com/e7lang/e7/blob/main/docs/EMBEDDING.md).

Values: `double` → number, signed integers → i64, unsigned integers → u64, `bool`, `string`,
`byte[]` (Uint8Array), `KeyHandle`, `null`. One `E7VM` must be used by one thread at a time.

License: MIT.
