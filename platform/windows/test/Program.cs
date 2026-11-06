// Tests for the C# binding (platform/windows/E7VM.cs) against the real e7vm shared library.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using E7;

static class Program
{
    static int failures;

    static void Check(bool cond, string what)
    {
        Console.WriteLine($"{(cond ? "ok  " : "FAIL")} {what}");
        if (!cond) failures++;
    }

    static uint Crc32(byte[] p, int n)
    {
        uint c = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++)
        {
            c ^= p[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        return ~c;
    }

    // ------------------------------------------------------------------ async host functions
    // Same scenarios as platform/android/shared-test/org/e7vm/AsyncScenarios.java.
    const string AsyncApp = @"declare function fetchKey(id: string): Promise<Uint8Array>;
declare function now(): Promise<number>;
export async function keyLength(id: string): Promise<number> {
  const k = await fetchKey(id);
  return k.length + (await now());
}
export async function both(): Promise<string> {
  const order: string[] = [];
  const pa = fetchKey('a').then((k) => { order.push('a'); return k; });
  const pb = fetchKey('b').then((k) => { order.push('b'); return k; });
  const [a, b] = await Promise.all([pa, pb]);
  return `${a.length},${b.length},${order.join('')}`;
}
export async function failing(): Promise<number> {
  try {
    await fetchKey('x');
    return 1;
  } catch (e) {
    if (e instanceof Error) console.log('caught ' + e.message);
    return -1;
  }
}
export async function stamp(): Promise<number> { return (await now()) * 2; }
export function syncCall(): number { return 3; }";

    // A VM with the async natives fetchKey (completed by the test) and now (completed at once).
    sealed class AsyncHost : IDisposable
    {
        public readonly E7VM Vm = new E7VM();
        public readonly E7Module M;
        public readonly List<string> Printed = new List<string>();
        readonly System.Collections.Concurrent.BlockingCollection<(string Key, TaskCompletionSource<object?> Tcs)> _started =
            new System.Collections.Concurrent.BlockingCollection<(string, TaskCompletionSource<object?>)>();

        public AsyncHost()
        {
            Vm.SetPrintHandler(Printed.Add);
            Vm.RegisterAsyncNative("fetchKey", "s>y", args =>
            {
                var tcs = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
                _started.Add(((string)args[0]!, tcs));
                return tcs.Task;
            });
            Vm.RegisterAsyncNative("now", ">n", args => Task.FromResult<object?>(5.0));
            M = Vm.LoadSource(AsyncApp, "app.ts");
        }

        public (string Key, TaskCompletionSource<object?> Tcs) Next()
        {
            if (!_started.TryTake(out var op, 10000)) throw new Exception("the async native was not called");
            return op;
        }

        public void Dispose() => Vm.Dispose();
    }

    static void CompleteLater(TaskCompletionSource<object?> tcs, object? value, int ms = 30) =>
        Task.Run(async () => { await Task.Delay(ms); tcs.SetResult(value); });

    static async Task<Exception> ThrowsAsync(Func<Task> f)
    {
        try { await f(); } catch (Exception e) { return e; }
        throw new Exception("expected an exception");
    }

    static async Task AsyncTests()
    {
        using (var h = new AsyncHost())  // settled later from another thread
        {
            var call = h.Vm.CallAsync(h.M, "keyLength", "k1");
            var op = h.Next();
            Check(op.Key == "k1" && !call.IsCompleted && h.Vm.OpenTickets == 1, "async: keyLength waits for fetchKey('k1')");
            CompleteLater(op.Tcs, new byte[] { 1, 2, 3, 4 });
            Check(await call is double d && d == 9, "async: settled later from another thread (4 + 5)");
            Check(h.Vm.OpenTickets == 0, "async: no open ticket");
        }
        using (var h = new AsyncHost())  // settled at once
        {
            Check(h.Vm.Call(h.M, "stamp") is double d1 && d1 == 10, "async: Call of an async export whose native completed at once");
            Check(await h.Vm.CallAsync(h.M, "stamp") is double d2 && d2 == 10, "async: CallAsync, settled at once");
            var call = h.Vm.CallAsync(h.M, "keyLength", "k");
            h.Next().Tcs.SetResult(new byte[2]);  // on this thread
            Check(await call is double d3 && d3 == 7, "async: completed on the calling thread");
        }
        using (var h = new AsyncHost())  // rejections
        {
            var call = h.Vm.CallAsync(h.M, "failing");
            var op = h.Next();
            _ = Task.Run(() => op.Tcs.SetException(new InvalidOperationException("no such key")));
            Check(await call is double d && d == -1 && h.Printed.Count == 1 && h.Printed[0] == "caught no such key",
                  "async: rejection caught by the script");
            call = h.Vm.CallAsync(h.M, "keyLength", "gone");
            h.Next().Tcs.SetException(new Exception("gone"));
            var e = await ThrowsAsync(() => call) as E7Exception;
            Check(e != null && e.Status == E7Status.Runtime && e.Message.Contains("gone"), $"async: uncaught rejection fails the call ({e?.Message})");
            call = h.Vm.CallAsync(h.M, "keyLength", "bad");
            h.Next().Tcs.SetResult("not bytes");
            e = await ThrowsAsync(() => call) as E7Exception;
            Check(e != null && e.Status == E7Status.Runtime && e.Message.Contains("fetchKey"), $"async: wrong value type rejects ({e?.Message})");
        }
        using (var h = new AsyncHost())  // Promise.all, settled in reverse order
        {
            var call = h.Vm.CallAsync(h.M, "both");
            var a = h.Next();
            var b = h.Next();
            Check(a.Key == "a" && b.Key == "b", "async: both started fetchKey('a') and fetchKey('b')");
            CompleteLater(b.Tcs, new byte[4], 10);
            CompleteLater(a.Tcs, new byte[1], 150);
            Check(await call is string r && r == "1,4,ba", "async: Promise.all keeps the order, b settled first");
        }
        using (var h = new AsyncHost())  // a pending call blocks other calls
        {
            var call = h.Vm.CallAsync(h.M, "keyLength", "k1");
            var op = h.Next();
            Check(Throws(() => h.Vm.Call(h.M, "syncCall")).Status == E7Status.State, "async: Call while pending -> State");
            var e = await ThrowsAsync(() => h.Vm.CallAsync(h.M, "syncCall")) as E7Exception;
            Check(e?.Status == E7Status.State, "async: CallAsync while pending -> State");
            op.Tcs.SetResult(new byte[1]);
            Check(await call is double d && d == 6, "async: the pending call finishes");
            Check(h.Vm.Call(h.M, "syncCall") is double d2 && d2 == 3, "async: calls work again");
        }
        using (var h = new AsyncHost())  // abandoning a pending call
        {
            using var cts = new CancellationTokenSource();
            var call = h.Vm.CallAsync(h.M, "keyLength", new object?[] { "k1" }, cts.Token);
            var op = h.Next();
            cts.Cancel();
            Check(await ThrowsAsync(() => call) is OperationCanceledException, "async: cancelling abandons the call");
            Check(h.Vm.OpenTickets == 0 && h.Vm.Call(h.M, "syncCall") is double d && d == 3, "async: the VM is free after cancel");
            op.Tcs.SetResult(new byte[1]);  // a late completion is ignored
            call = h.Vm.CallAsync(h.M, "keyLength", "k2");
            op = h.Next();
            h.Vm.AbortSuspended();
            Check(await ThrowsAsync(() => call) is OperationCanceledException, "async: AbortSuspended abandons the call");
            op.Tcs.SetResult(new byte[1]);
            call = h.Vm.CallAsync(h.M, "keyLength", "k3");
            op = h.Next();
            op.Tcs.SetResult(new byte[3]);
            Check(op.Key == "k3" && await call is double d3 && d3 == 8, "async: a new call after AbortSuspended");
        }
        using (var h = new AsyncHost())  // a synchronous call of a pending export
        {
            var e = Throws(() => h.Vm.Call(h.M, "keyLength", "k1"));
            Check(e.IsPending && e.Message.Contains("CallAsync"), $"async: synchronous Call -> Pending ({e.Message})");
            h.Next().Tcs.SetResult(new byte[1]);  // ignored: the call was abandoned
            Check(h.Vm.Call(h.M, "syncCall") is double d && d == 3, "async: the VM is usable after the Pending error");
        }
        // top-level await
        const string boot = @"declare function fetchKey(id: string): Promise<Uint8Array>;
const key = await fetchKey('boot');
export function keySize(): number { return key.length; }";
        var bootCode = E7VM.Compile(boot, "boot.ts");
        using (var vm = new E7VM())
        {
            vm.RegisterAsyncNative("fetchKey", "s>y", async args =>
            {
                await Task.Delay(10);
                return new byte[16];
            });
            Check(Throws(() => vm.Load(bootCode)).IsPending, "async: Load of a module with top-level await -> Pending");
            var m = await vm.LoadAsync(bootCode);
            Check(vm.Call(m, "keySize") is double d && d == 16, "async: LoadAsync finishes the top-level code");
        }
    }

    static E7Exception Throws(Action a)
    {
        try { a(); } catch (E7Exception e) { return e; }
        throw new Exception("expected E7Exception");
    }

    static int Main(string[] args)
    {
        // With a directory argument, resolve "e7vm" from it (libe7vm.so / .dylib / e7vm.dll, as built by CMake).
        // Without one, the default probing applies (e.g. runtimes/<rid>/native/ of the E7VM NuGet package).
        if (args.Length > 0)
        {
            string dir = Path.GetFullPath(args[0]);
            NativeLibrary.SetDllImportResolver(typeof(E7VM).Assembly, (name, asm, path) =>
            {
                if (name != "e7vm") return IntPtr.Zero;
                foreach (var f in new[] { "e7vm.dll", "libe7vm.so", "libe7vm.dylib", Path.Combine("Release", "e7vm.dll") })
                    if (File.Exists(Path.Combine(dir, f))) return NativeLibrary.Load(Path.Combine(dir, f));
                return IntPtr.Zero;
            });
        }

        Console.WriteLine($"e7vm {E7VM.Version}");
        const string signSrc = @"import { hmac } from ""e7:crypto"";
import { hexEncode } from ""e7:encoding"";
export function sign(message: string, secret: string): string {
  const enc = new TextEncoder();
  return hexEncode(hmac(""sha256"", enc.encode(secret), enc.encode(message)));
}
";
        const string expected = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";

        // compile + load + call
        byte[] code = E7VM.Compile(signSrc, "sign.ts");
        using (var vm = new E7VM(memoryLimit: 32 << 20))
        {
            var m = vm.Load(code);
            Check(vm.HasFunction(m, "sign") && !vm.HasFunction(m, "nope"), "HasFunction");
            vm.SetBudget(10_000_000);
            Check((string?)vm.Call(m, "sign", "amount=100&currency=CNY", "s3cr3t") == expected, "Compile + Load + Call (string args/result)");
            var e = Throws(() => vm.Call(m, "nope"));
            Check(e.Status == E7Status.NotFound, "missing export -> NotFound");
            e = Throws(() => vm.Call(m, "sign", 1.0, "x"));
            Check(e.Status == E7Status.Args, "wrong argument type -> Args");
        }

        // compile errors
        var ce = Throws(() => E7VM.Compile("let x: number = 'a';", "bad.ts"));
        Check(ce.Status == E7Status.Compile && ce.Message.Contains("bad.ts:1"), "compile error with location");

        // values, natives, print, key provider
        using (var vm = new E7VM())
        {
            var printed = new List<string>();
            vm.SetPrintHandler(s => printed.Add(s));
            vm.RegisterNative("hostAdd", "nn>n", a => (double)a[0]! + (double)a[1]!);
            vm.RegisterNative("hostTag", "s>s", a => "<" + (string)a[0]! + ">");
            vm.RegisterNative("hostInt", "7>7", a => (long)a[0]! * 2);
            vm.RegisterNative("hostCount", ">5", a => 41);         // int coerced to i32
            vm.RegisterNative("hostFail", "s>v", a => throw new InvalidOperationException("host says " + (string)a[0]!));
            vm.SetKeyProvider(id => id == 7 ? Encoding.UTF8.GetBytes("s3cr3t") : null);
            var m = vm.LoadSource(@"import { hmac } from ""e7:crypto"";
import type { KeyHandle } from ""e7:crypto"";
import { hexEncode } from ""e7:encoding"";
declare function hostAdd(a: number, b: number): number;
declare function hostTag(s: string): string;
declare function hostInt(x: i64): i64;
declare function hostCount(): i32;
declare function hostFail(s: string): void;
console.log(""loaded "" + hostTag(""é✓""));
export function add(a: number, b: number): number { return hostAdd(a, b); }
export function twice(x: i64): i64 { return hostInt(x); }
export function count(): i32 { return hostCount() + 1; }
export function rev(b: Uint8Array): Uint8Array { return b.slice().reverse(); }
export function flip(b: boolean): boolean { return !b; }
export function big(x: u64): u64 { return x - 1; }
export function mac(k: KeyHandle, m: string): string { return hexEncode(hmac(""sha256"", k, new TextEncoder().encode(m))); }
export function fail(): void { hostFail(""no""); }
export function nothing(): void {}
", "t.ts");
            Check(printed.Count == 1 && printed[0] == "loaded <é✓>", "print handler + UTF-8 natives");
            Check((double?)vm.Call(m, "add", 1.5, 2.25) == 3.75, "number args via native");
            Check((long?)vm.Call(m, "twice", -21L) == -42, "i64 via native");
            Check((long?)vm.Call(m, "count") == 42, "native result coerced to signature type");
            var rev = (byte[]?)vm.Call(m, "rev", new byte[] { 1, 2, 3 });
            Check(rev != null && rev.Length == 3 && rev[0] == 3 && rev[2] == 1, "Uint8Array round trip");
            Check((bool?)vm.Call(m, "flip", true) == false, "boolean");
            Check((ulong?)vm.Call(m, "big", 0UL) == ulong.MaxValue, "u64 wraps");
            Check((string?)vm.Call(m, "mac", new KeyHandle(7), "amount=100&currency=CNY") == expected, "KeyHandle resolved by key provider");
            var e = Throws(() => vm.Call(m, "mac", new KeyHandle(8), "x"));
            Check(e.Status == E7Status.Runtime, "unknown key -> runtime error");
            e = Throws(() => vm.Call(m, "fail"));
            Check(e.Status == E7Status.Runtime && e.Message.Contains("host says no"), "host exception -> script runtime error");
            Check(vm.Call(m, "nothing") == null, "void -> null");
            Check(vm.MemoryUsed > 0, "MemoryUsed");
        }

        // limits
        using (var vm = new E7VM(memoryLimit: 8 << 20))
        {
            var m = vm.LoadSource("export function spin(): void { while (true) {} }\nexport function grow(): number { const a: number[] = []; while (true) a.push(1); return 0; }");
            vm.SetBudget(1_000_000);
            Check(Throws(() => vm.Call(m, "spin")).Status == E7Status.Budget, "instruction budget");
            vm.SetBudget(0);
            Check(Throws(() => vm.Call(m, "grow")).Status == E7Status.Memory, "memory limit");
        }
        using (var vm = new E7VM(suspendOnBudget: true))
        {
            var m = vm.LoadSource("export function sum(n: number): number { let s = 0; for (let i = 0; i < n; i++) s += i; return s; }");
            vm.SetBudget(100_000);
            object? r = null;
            int slices = 0;
            try { r = vm.Call(m, "sum", 1_000_000.0); }
            catch (E7Exception e) when (e.IsSuspended)
            {
                while (true)
                {
                    slices++;
                    vm.SetBudget(100_000);  // each slice gets a fresh budget
                    try { r = vm.Resume(); break; } catch (E7Exception e2) when (e2.IsSuspended) { }
                }
            }
            Check(slices > 1 && slices < 100 && (double?)r == 499999500000.0, $"suspend + resume ({slices} slices)");
        }

        // optional signing
        var seed = RandomNumberGenerator.GetBytes(32);
        var pub = E7VM.PublicKey(seed);
        var signedCode = E7VM.Sign(code, seed);
        using (var vm = new E7VM())
        {
            Check((string?)vm.Call(vm.Load(signedCode), "sign", "amount=100&currency=CNY", "s3cr3t") == expected, "signed module runs without trusted keys");
        }
        using (var vm = new E7VM(0, false, pub))
        {
            Check((string?)vm.Call(vm.Load(signedCode), "sign", "amount=100&currency=CNY", "s3cr3t") == expected, "signed module accepted by trusted key");
            Check(Throws(() => vm.Load(code)).Status == E7Status.Signature, "unsigned module rejected when keys are trusted");
        }
        using (var vm = new E7VM(0, false, E7VM.PublicKey(RandomNumberGenerator.GetBytes(32))))
        {
            Check(Throws(() => vm.Load(signedCode)).Status == E7Status.Signature, "module signed by another key rejected");
        }
        var tampered = (byte[])code.Clone();
        tampered[tampered.Length / 2] ^= 0x40;
        using (var vm = new E7VM())
        {
            var st = Throws(() => vm.Load(tampered)).Status;
            Check(st == E7Status.Load || st == E7Status.Verify, "tampered bytecode rejected");
        }

        // 0.2 API: version number, inspect, GC statistics and growth setting, detached signatures
        uint vn = E7VM.VersionNumber;
        Check(E7VM.Version == $"{vn >> 16}.{(vn >> 8) & 0xFF}.{vn & 0xFF}" && vn >= 0x000200, "VersionNumber");
        var info = E7VM.Inspect(signedCode);
        Check(info.CanRun && info.Format == 2 && info.RequiresVersionString == "0.2.0" && info.IsSigned && !info.HasDebugInfo
              && info.KeyId.Length == 8 && Array.Exists(info.KeyId, b => b != 0), "Inspect signed module");
        Check(!E7VM.Inspect(code).IsSigned, "Inspect unsigned module");
        var newer = (byte[])code.Clone();
        BitConverter.GetBytes(0x630000u).CopyTo(newer, 12);  // `requires` header field: e7vm 99.0.0
        BitConverter.GetBytes(Crc32(newer, newer.Length - 8)).CopyTo(newer, newer.Length - 4);
        info = E7VM.Inspect(newer);
        Check(info.Status == E7Status.Version && info.RequiresVersionString == "99.0.0", "Inspect: module needs a newer VM");
        using (var vm = new E7VM())
        {
            Check(Throws(() => vm.Load(newer)).Status == E7Status.Version, "Load: module needs a newer VM -> Version");
        }
        Check(E7VM.Inspect(new byte[] { 1, 2, 3 }).Status == E7Status.Load, "Inspect garbage -> Load");
        using (var vm = new E7VM(0, false, 150u))
        {
            var m = vm.LoadSource("let keep: number[][] = [];\nexport function churn(): number { for (let i = 0; i < 20000; i++) { keep.push([i]); if (keep.length > 500) keep = []; } return keep.length; }");
            vm.Call(m, "churn");
            vm.CollectGarbage();
            var gs = vm.GcStats();
            Check(gs.Collections >= 1 && gs.MaxPauseNanoseconds > 0 && gs.TotalPauseNanoseconds >= gs.MaxPauseNanoseconds, $"GcStats {gs}");
            Check(gs.HeapBytes == vm.MemoryUsed && gs.PeakHeapBytes >= gs.HeapBytes, "GcStats heap");
        }
        var katSeed = new byte[32];
        for (int i = 0; i < 32; i++) katSeed[i] = (byte)(i + 1);
        var manifest = Encoding.UTF8.GetBytes("{\"version\":\"2\",\"killSwitch\":false}");
        var dsig = E7VM.SignDetached(manifest, katSeed);
        Check(Convert.ToHexString(dsig).ToLowerInvariant() == "1549a30db54e6fc64e15efee8a9b5222a22be751830dd98dd5f97d30c1bc6b09"
              + "2cdaa2efdc5a609a338b827ca32a0a9d1d9212ffd3eb51bfc9eb3b073487e607", "SignDetached known answer");
        var katPub = E7VM.PublicKey(katSeed);
        Check(E7VM.VerifyDetached(manifest, dsig, katPub) && E7VM.VerifyDetached(manifest, dsig, pub, katPub), "VerifyDetached (any trusted key)");
        Check(!E7VM.VerifyDetached(manifest, dsig, pub) && !E7VM.VerifyDetached(Encoding.UTF8.GetBytes("x"), dsig, katPub), "VerifyDetached rejects");

        AsyncTests().GetAwaiter().GetResult();

        if (failures > 0) { Console.WriteLine($"{failures} check(s) failed"); return 1; }
        Console.WriteLine("all C# binding tests passed");
        return 0;
    }
}
