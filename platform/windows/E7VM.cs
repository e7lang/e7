// C# wrapper around the e7vm C API (P/Invoke). Works with e7vm.dll on Windows and with
// libe7vm.so / libe7vm.dylib on Linux / macOS (64-bit processes). Build the shared library with
//   platform/windows/build_windows.ps1 -Shared        (Windows)
//   cmake -DE7_BUILD_SHARED=ON ...                    (any platform)
//
//   using var vm = new E7.E7VM(memoryLimit: 32 << 20);        // optional: trusted public keys
//   vm.SetKeyProvider(id => LoadKey(id));                     // keys never enter script memory
//   var module = vm.Load(File.ReadAllBytes("app.e7b"));
//   vm.SetBudget(10_000_000);
//   var sig = (string)vm.Call(module, "sign", "a=1&b=2", new E7.KeyHandle(1))!;
//
// Values: double/float -> number, sbyte/short/int/long -> i64, byte/ushort/uint/ulong -> u64,
// bool, string, byte[] (Uint8Array), KeyHandle, null. Results come back as double, long, ulong,
// bool, string, byte[], KeyHandle or null (void).
//
// Thread safety: like the C API, one E7VM must be used by one thread at a time.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace E7
{
    /// <summary>Opaque reference to a key held by the app (DPAPI, a vault, ...), resolved by the key provider.</summary>
    public readonly struct KeyHandle : IEquatable<KeyHandle>
    {
        public long Id { get; }
        public KeyHandle(long id) { Id = id; }
        public bool Equals(KeyHandle other) => Id == other.Id;
        public override bool Equals(object? obj) => obj is KeyHandle k && Equals(k);
        public override int GetHashCode() => Id.GetHashCode();
        public override string ToString() => $"KeyHandle({Id})";
    }

    /// <summary>Status codes of the C API (e7_status).</summary>
    public enum E7Status
    {
        Ok = 0, Suspended = 1, Runtime = 2, Budget = 3, Memory = 4, Stack = 5, Load = 6, Verify = 7,
        Signature = 8, NotFound = 9, Args = 10, Compile = 11, State = 12,
    }

    public sealed class E7Exception : Exception
    {
        public E7Status Status { get; }
        public bool IsSuspended => Status == E7Status.Suspended;
        public E7Exception(E7Status status, string message) : base(message) { Status = status; }
    }

    public sealed class E7Module
    {
        internal readonly IntPtr Ptr;
        internal E7Module(IntPtr p) { Ptr = p; }
    }

    public sealed class E7VM : IDisposable
    {
        // ------------------------------------------------------------------ native interop
        private const string Lib = "e7vm";

        private enum ValueType : int { Void = 0, Number = 1, Bool = 2, Int = 3, UInt = 4, String = 5, Bytes = 6, Handle = 7, Null = 8 }

        // e7_value on 64-bit targets: { int type; union { double; int; int64; uint64; {ptr, size_t}; int64 } v; }
        [StructLayout(LayoutKind.Explicit, Size = 24)]
        private struct Value
        {
            [FieldOffset(0)] public ValueType Type;
            [FieldOffset(8)] public double Num;
            [FieldOffset(8)] public int Boolean;
            [FieldOffset(8)] public long I;
            [FieldOffset(8)] public ulong U;
            [FieldOffset(8)] public IntPtr Ptr;
            [FieldOffset(16)] public UIntPtr Len;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Config
        {
            public UIntPtr MemoryLimit;
            public uint StackSlots;
            public uint MaxFrames;
            public int SuspendOnBudget;
            public int RequireSignature;
            public IntPtr TrustedKeys;
            public UIntPtr NumTrustedKeys;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeFn(IntPtr vm, IntPtr args, UIntPtr nargs, IntPtr ret, IntPtr user);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void PrintFn(IntPtr user, IntPtr text, UIntPtr len);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int KeyFn(IntPtr user, long id, IntPtr key, IntPtr len);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_config_init(ref Config cfg);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr e7_vm_new(ref Config cfg);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_vm_free(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr e7_version();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_load(IntPtr vm, byte[] data, UIntPtr len, out IntPtr module);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_compile(byte[] source, UIntPtr len, byte[] fileName, out IntPtr output, out UIntPtr outLen, out IntPtr errors);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_load_source(IntPtr vm, byte[] source, UIntPtr len, byte[] fileName, out IntPtr module);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_free(IntPtr p);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_call(IntPtr vm, IntPtr module, byte[] function, [In] Value[] args, UIntPtr nargs, out Value ret);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_resume(IntPtr vm, out Value ret);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_abort_suspended(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_set_budget(IntPtr vm, long ticks);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr e7_last_error(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_has_function(IntPtr module, byte[] function);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_register_native(IntPtr vm, byte[] name, byte[] signature, NativeFn fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_native_error(IntPtr vm, byte[] message);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_set_print(IntPtr vm, PrintFn? fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_set_key_provider(IntPtr vm, KeyFn? fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_seed_random(IntPtr vm, ulong seed);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern UIntPtr e7_memory_used(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_collect_garbage(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_sign_module(byte[] e7b, UIntPtr len, byte[] seed, out IntPtr output, out UIntPtr outLen);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_public_key(byte[] seed, byte[] publicKey);

        // ------------------------------------------------------------------ state
        private IntPtr _vm;
        private readonly List<NativeBox> _natives = new List<NativeBox>();
        private PrintFn? _printThunk;
        private Action<string>? _print;
        private KeyFn? _keyThunk;
        private Func<long, byte[]?>? _keys;
        private IntPtr _keyBuf;
        private int _keyLen;

        private sealed class NativeBox
        {
            public NativeFn Thunk = null!;
            public Func<object?[], object?> Fn = null!;
            public char ReturnCode;
            public readonly List<IntPtr> RetBuffers = new List<IntPtr>();
        }

        /// <summary>Creates a VM. With trusted keys, only modules signed by one of them can be loaded.</summary>
        /// <param name="memoryLimit">heap limit in bytes (0: default 256 MiB)</param>
        /// <param name="suspendOnBudget">budget exhaustion suspends (resume with <see cref="Resume"/>) instead of failing</param>
        /// <param name="trustedKeys">Ed25519 public keys (32 bytes each); empty: signatures are not required</param>
        public E7VM(long memoryLimit = 0, bool suspendOnBudget = false, params byte[][] trustedKeys)
        {
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("E7VM requires a 64-bit process");
            var cfg = new Config();
            e7_config_init(ref cfg);
            if (memoryLimit > 0) cfg.MemoryLimit = (UIntPtr)(ulong)memoryLimit;
            cfg.SuspendOnBudget = suspendOnBudget ? 1 : 0;
            IntPtr keys = IntPtr.Zero;
            try
            {
                if (trustedKeys != null && trustedKeys.Length > 0)
                {
                    keys = Marshal.AllocHGlobal(32 * trustedKeys.Length);
                    for (int i = 0; i < trustedKeys.Length; i++)
                    {
                        if (trustedKeys[i] == null || trustedKeys[i].Length != 32)
                            throw new ArgumentException("public keys must be 32 bytes", nameof(trustedKeys));
                        Marshal.Copy(trustedKeys[i], 0, keys + 32 * i, 32);
                    }
                    cfg.RequireSignature = 1;
                    cfg.TrustedKeys = keys;
                    cfg.NumTrustedKeys = (UIntPtr)(uint)trustedKeys.Length;
                }
                _vm = e7_vm_new(ref cfg);  // keys are copied by the VM
            }
            finally
            {
                if (keys != IntPtr.Zero) Marshal.FreeHGlobal(keys);
            }
            if (_vm == IntPtr.Zero) throw new E7Exception(E7Status.Memory, "cannot create VM");
        }

        public static string Version => PtrToUtf8(e7_version(), -1);

        public void Dispose()
        {
            if (_vm != IntPtr.Zero)
            {
                e7_vm_free(_vm);
                _vm = IntPtr.Zero;
            }
            ClearKey();
            foreach (var n in _natives) FreeAll(n.RetBuffers);
            _natives.Clear();
            GC.SuppressFinalize(this);
        }

        ~E7VM()
        {
            if (_vm != IntPtr.Zero) e7_vm_free(_vm);
            if (_keyBuf != IntPtr.Zero) Marshal.FreeHGlobal(_keyBuf);
        }

        private IntPtr Handle => _vm != IntPtr.Zero ? _vm : throw new ObjectDisposedException(nameof(E7VM));

        private void Check(int status)
        {
            if (status != 0) throw new E7Exception((E7Status)status, PtrToUtf8(e7_last_error(_vm), -1));
        }

        // ------------------------------------------------------------------ loading and calling
        /// <summary>Verifies (signature if required, structure, types), links natives and runs the module's top-level code.</summary>
        public E7Module Load(byte[] bytecode)
        {
            if (bytecode == null) throw new ArgumentNullException(nameof(bytecode));
            Check(e7_load(Handle, bytecode, (UIntPtr)(uint)bytecode.Length, out var m));
            return new E7Module(m);
        }

        /// <summary>Compiles and loads source (development; needs the compiler in the library).</summary>
        public E7Module LoadSource(string source, string fileName = "main.ts")
        {
            var src = Encoding.UTF8.GetBytes(source);
            Check(e7_load_source(Handle, src, (UIntPtr)(uint)src.Length, Z(fileName), out var m));
            return new E7Module(m);
        }

        public bool HasFunction(E7Module module, string function) => e7_has_function(module.Ptr, Z(function)) != 0;

        /// <summary>Calls an exported function. Throws <see cref="E7Exception"/> on errors (IsSuspended when the budget suspends).</summary>
        public object? Call(E7Module module, string function, params object?[] args)
        {
            if (module == null) throw new ArgumentNullException(nameof(module));
            args ??= new object?[] { null };
            var buffers = new List<IntPtr>();
            try
            {
                var vals = new Value[Math.Max(args.Length, 1)];
                for (int i = 0; i < args.Length; i++) vals[i] = ToNative(args[i], buffers);
                Check(e7_call(Handle, module.Ptr, Z(function), vals, (UIntPtr)(uint)args.Length, out var ret));
                return FromNative(ret);
            }
            finally
            {
                FreeAll(buffers);
            }
        }

        public object? Resume()
        {
            Check(e7_resume(Handle, out var ret));
            return FromNative(ret);
        }

        public void AbortSuspended() => e7_abort_suspended(Handle);

        /// <summary>Instruction budget for subsequent execution (&lt;= 0: unlimited).</summary>
        public void SetBudget(long ticks) => e7_set_budget(Handle, ticks);
        public void SeedRandom(ulong seed) => e7_seed_random(Handle, seed);
        public long MemoryUsed => (long)(ulong)e7_memory_used(Handle);
        public void CollectGarbage() => e7_collect_garbage(Handle);

        // ------------------------------------------------------------------ host services
        /// <summary>Receives console.log output (one call per line, without the newline).</summary>
        public void SetPrintHandler(Action<string>? handler)
        {
            _print = handler;
            if (handler == null) { e7_set_print(Handle, null, IntPtr.Zero); _printThunk = null; return; }
            _printThunk = (user, text, len) => { try { _print?.Invoke(PtrToUtf8(text, (long)(ulong)len)); } catch { } };
            e7_set_print(Handle, _printThunk, IntPtr.Zero);
        }

        /// <summary>Resolves KeyHandle ids to key bytes (return null if unknown). Keys never become script values.</summary>
        public void SetKeyProvider(Func<long, byte[]?>? provider)
        {
            _keys = provider;
            if (provider == null) { e7_set_key_provider(Handle, null, IntPtr.Zero); _keyThunk = null; return; }
            _keyThunk = (user, id, keyOut, lenOut) =>
            {
                try
                {
                    var k = _keys?.Invoke(id);
                    if (k == null) return -1;
                    ClearKey();
                    _keyBuf = Marshal.AllocHGlobal(Math.Max(k.Length, 1));
                    _keyLen = k.Length;
                    Marshal.Copy(k, 0, _keyBuf, k.Length);
                    Marshal.WriteIntPtr(keyOut, _keyBuf);
                    Marshal.WriteIntPtr(lenOut, (IntPtr)k.Length);
                    return 0;
                }
                catch
                {
                    return -1;
                }
            };
            e7_set_key_provider(Handle, _keyThunk, IntPtr.Zero);
        }

        private void ClearKey()
        {
            if (_keyBuf == IntPtr.Zero) return;
            for (int i = 0; i < _keyLen; i++) Marshal.WriteByte(_keyBuf, i, 0);  // wipe the previous key
            Marshal.FreeHGlobal(_keyBuf);
            _keyBuf = IntPtr.Zero;
            _keyLen = 0;
        }

        /// <summary>
        /// Registers a host function before <see cref="Load"/>. signature: parameter codes, '&gt;', return code
        /// (n number, b boolean, s string, y Uint8Array, h KeyHandle, v void, 1..8 i8 u8 i16 u16 i32 u32 i64 u64),
        /// e.g. "sn&gt;s". Scripts declare it with <c>declare function name(a: string, b: number): string;</c>
        /// An exception thrown by <paramref name="fn"/> becomes a runtime error in the script.
        /// </summary>
        public void RegisterNative(string name, string signature, Func<object?[], object?> fn)
        {
            int gt = signature.IndexOf('>');
            if (gt < 0 || gt + 1 >= signature.Length) throw new ArgumentException("signature must look like \"sn>s\"", nameof(signature));
            var box = new NativeBox { Fn = fn, ReturnCode = signature[gt + 1] };
            box.Thunk = (vm, args, nargs, ret, user) =>
            {
                try
                {
                    int n = (int)(ulong)nargs;
                    var vals = new object?[n];
                    int size = Marshal.SizeOf<Value>();
                    for (int i = 0; i < n; i++) vals[i] = FromNative(Marshal.PtrToStructure<Value>(args + i * size));
                    var r = box.Fn(vals);
                    FreeAll(box.RetBuffers);  // the VM copied the previous result already
                    var rv = box.ReturnCode == 'v' ? new Value { Type = ValueType.Void } : ToNative(Coerce(r, box.ReturnCode), box.RetBuffers);
                    Marshal.StructureToPtr(rv, ret, false);
                    return 0;
                }
                catch (Exception e)
                {
                    e7_native_error(vm, Z(e.Message));
                    return (int)E7Status.Runtime;
                }
            };
            _natives.Add(box);  // keeps the delegate alive as long as the VM
            Check(e7_register_native(Handle, Z(name), Z(signature), box.Thunk, IntPtr.Zero));
        }

        // ------------------------------------------------------------------ static helpers
        /// <summary>Compiles E7 source to bytecode (.e7b). Throws E7Exception(Compile) with the diagnostics.</summary>
        public static byte[] Compile(string source, string fileName = "main.ts")
        {
            var src = Encoding.UTF8.GetBytes(source);
            int st = e7_compile(src, (UIntPtr)(uint)src.Length, Z(fileName), out var output, out var len, out var errors);
            try
            {
                if (st != 0) throw new E7Exception((E7Status)st, errors != IntPtr.Zero ? PtrToUtf8(errors, -1) : "compile error");
                var bytes = new byte[(int)(ulong)len];
                Marshal.Copy(output, bytes, 0, bytes.Length);
                return bytes;
            }
            finally
            {
                if (output != IntPtr.Zero) e7_free(output);
                if (errors != IntPtr.Zero) e7_free(errors);
            }
        }

        /// <summary>Appends an Ed25519 signature trailer to a module (seed: the 32-byte secret key).</summary>
        public static byte[] Sign(byte[] bytecode, byte[] seed)
        {
            if (seed == null || seed.Length != 32) throw new ArgumentException("seed must be 32 bytes", nameof(seed));
            int st = e7_sign_module(bytecode, (UIntPtr)(uint)bytecode.Length, seed, out var output, out var len);
            if (st != 0) throw new E7Exception((E7Status)st, "signing failed");
            try
            {
                var bytes = new byte[(int)(ulong)len];
                Marshal.Copy(output, bytes, 0, bytes.Length);
                return bytes;
            }
            finally
            {
                e7_free(output);
            }
        }

        public static byte[] PublicKey(byte[] seed)
        {
            if (seed == null || seed.Length != 32) throw new ArgumentException("seed must be 32 bytes", nameof(seed));
            var pk = new byte[32];
            e7_public_key(seed, pk);
            return pk;
        }

        // ------------------------------------------------------------------ marshaling
        private static byte[] Z(string s)
        {
            var b = Encoding.UTF8.GetBytes(s);
            Array.Resize(ref b, b.Length + 1);
            return b;
        }

        private static string PtrToUtf8(IntPtr p, long len)
        {
            if (p == IntPtr.Zero) return "";
            if (len < 0)
            {
                len = 0;
                while (Marshal.ReadByte(p, (int)len) != 0) len++;
            }
            var b = new byte[len];
            Marshal.Copy(p, b, 0, (int)len);
            return Encoding.UTF8.GetString(b);
        }

        private static IntPtr Copy(byte[] data, List<IntPtr> buffers)
        {
            var p = Marshal.AllocHGlobal(Math.Max(data.Length, 1));
            buffers.Add(p);
            Marshal.Copy(data, 0, p, data.Length);
            return p;
        }

        private static void FreeAll(List<IntPtr> buffers)
        {
            foreach (var p in buffers) Marshal.FreeHGlobal(p);
            buffers.Clear();
        }

        private static Value ToNative(object? a, List<IntPtr> buffers)
        {
            var v = new Value();
            switch (a)
            {
                case null: v.Type = ValueType.Null; break;
                case double d: v.Type = ValueType.Number; v.Num = d; break;
                case float f: v.Type = ValueType.Number; v.Num = f; break;
                case bool b: v.Type = ValueType.Bool; v.Boolean = b ? 1 : 0; break;
                case sbyte x: v.Type = ValueType.Int; v.I = x; break;
                case short x: v.Type = ValueType.Int; v.I = x; break;
                case int x: v.Type = ValueType.Int; v.I = x; break;
                case long x: v.Type = ValueType.Int; v.I = x; break;
                case byte x: v.Type = ValueType.UInt; v.U = x; break;
                case ushort x: v.Type = ValueType.UInt; v.U = x; break;
                case uint x: v.Type = ValueType.UInt; v.U = x; break;
                case ulong x: v.Type = ValueType.UInt; v.U = x; break;
                case string s:
                {
                    var bytes = Encoding.UTF8.GetBytes(s);
                    v.Type = ValueType.String; v.Ptr = Copy(bytes, buffers); v.Len = (UIntPtr)(uint)bytes.Length;
                    break;
                }
                case byte[] bytes: v.Type = ValueType.Bytes; v.Ptr = Copy(bytes, buffers); v.Len = (UIntPtr)(uint)bytes.Length; break;
                case KeyHandle h: v.Type = ValueType.Handle; v.I = h.Id; break;
                default: throw new ArgumentException($"unsupported argument type {a.GetType()}");
            }
            return v;
        }

        private static object? FromNative(Value v)
        {
            switch (v.Type)
            {
                case ValueType.Number: return v.Num;
                case ValueType.Bool: return v.Boolean != 0;
                case ValueType.Int: return v.I;
                case ValueType.UInt: return v.U;
                case ValueType.String: return PtrToUtf8(v.Ptr, (long)(ulong)v.Len);
                case ValueType.Bytes:
                {
                    var b = new byte[(int)(ulong)v.Len];
                    if (b.Length > 0) Marshal.Copy(v.Ptr, b, 0, b.Length);
                    return b;
                }
                case ValueType.Handle: return new KeyHandle(v.I);
                default: return null;
            }
        }

        // Converts a host function's result to the type its signature declares (e.g. int -> number).
        private static object? Coerce(object? r, char code)
        {
            if (r == null || code == 'v') return null;
            switch (code)
            {
                case 'n': return Convert.ToDouble(r);
                case '1': case '3': case '5': case '7': return Convert.ToInt64(r);
                case '2': case '4': case '6': case '8': return Convert.ToUInt64(r);
                case 'b': return Convert.ToBoolean(r);
                default: return r;
            }
        }
    }
}
