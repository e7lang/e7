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

Async host functions (E7 0.3): a script that declares
`declare function fetchKey(id: string): Promise<Uint8Array>;` awaits a `Task` of the host, and `CallAsync`
returns an async export's value:

```csharp
vm.RegisterAsyncNative("fetchKey", "s>y", async args => await keyStore.FetchAsync((string)args[0]!));
var module = await vm.LoadAsync(code);              // top-level code may await too
var sig = (string?)await vm.CallAsync(module, "sign", "k1", body);
```

Completions are settled on the caller's context (e.g. the UI thread) in the order they finish; a synchronous
`Call` that would wait throws `E7Exception` with `E7Status.Pending`. Cancel with the
`CallAsync(module, fn, args, cancellationToken)` overload or `AbortSuspended()`.

`CallAwait` / `LoadAwait` are the blocking variants (the calling thread waits and settles the results).

Over-the-air updates: `E7ModuleManager` downloads modules named by a signed manifest, with staged rollout, kill
switch, `minVm`, `selfTest()`, last-known-good storage and rollback after consecutive failures (the same manager as
in the Java and Swift bindings; see the
[deployment guide](https://github.com/e7lang/e7/blob/main/docs/DEPLOYMENT.md)):

```csharp
using var mm = new E7ModuleManager(new E7ModuleManager.Options
{
    ManifestUrl = new Uri("https://updates.example.com/pricing/manifest.json"),
    TrustedKeys = { releaseKey },                       // Ed25519 public keys (32 bytes)
    Fetcher = url => http.GetByteArrayAsync(url).GetAwaiter().GetResult(),
    StorageDirectory = Path.Combine(appData, "e7"),
    UserId = installationId,
    FallbackModule = bundledModule,
    ReportError = (version, status, message) => log.Warn($"{version} {status}: {message}"),
});
mm.Start();                                         // last-known-good from disk, or the fallback
await Task.Run(() => mm.Update());                  // manifest, download, verify, selfTest, activate
var price = mm.Active!.Call("price", order);        // or await mm.Active!.CallAsync(...) for async exports
```

Host functions, key providers, print handlers, suspend/resume and module signing are described in the
[embedding guide](https://github.com/e7lang/e7/blob/main/docs/EMBEDDING.md) (async host functions:
[§2.6](https://github.com/e7lang/e7/blob/main/docs/EMBEDDING.md#26-async-host-functions)).

Values: `double` → number, signed integers → i64, unsigned integers → u64, `bool`, `string`,
`byte[]` (Uint8Array), `KeyHandle`, `null`. One `E7VM` must be used by one thread at a time.

License: MIT.
