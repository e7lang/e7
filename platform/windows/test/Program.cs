// Tests for the C# binding (platform/windows/E7VM.cs) against the real e7vm shared library.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using E7;

static class Program
{
    static int failures;

    static void Check(bool cond, string what)
    {
        Console.WriteLine($"{(cond ? "ok  " : "FAIL")} {what}");
        if (!cond) failures++;
    }

    static E7Exception Throws(Action a)
    {
        try { a(); } catch (E7Exception e) { return e; }
        throw new Exception("expected E7Exception");
    }

    static int Main(string[] args)
    {
        // Resolve "e7vm" from the directory given on the command line (libe7vm.so / .dylib / e7vm.dll).
        string? dir = args.Length > 0 ? Path.GetFullPath(args[0]) : null;
        NativeLibrary.SetDllImportResolver(typeof(E7VM).Assembly, (name, asm, path) =>
        {
            if (name != "e7vm" || dir == null) return IntPtr.Zero;
            foreach (var f in new[] { "e7vm.dll", "libe7vm.so", "libe7vm.dylib", Path.Combine("Release", "e7vm.dll") })
                if (File.Exists(Path.Combine(dir, f))) return NativeLibrary.Load(Path.Combine(dir, f));
            return IntPtr.Zero;
        });

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
                    try { r = vm.Resume(); break; } catch (E7Exception e2) when (e2.IsSuspended) { }
                }
            }
            Check(slices > 1 && (double?)r == 499999500000.0, $"suspend + resume ({slices} slices)");
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

        if (failures > 0) { Console.WriteLine($"{failures} check(s) failed"); return 1; }
        Console.WriteLine("all C# binding tests passed");
        return 0;
    }
}
