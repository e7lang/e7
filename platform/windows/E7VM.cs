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
//
// Async host functions: `declare function fetchKey(id: string): Promise<Uint8Array>;` in the script,
//   vm.RegisterAsyncNative("fetchKey", "s>y", async args => await keys.FetchAsync((string)args[0]!));
//   var sig = await vm.CallAsync(module, "sign", "k1", payload);
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

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
        Signature = 8, NotFound = 9, Args = 10, Compile = 11, State = 12, Paused = 13,
        /// <summary>The module's bytecode format or required e7vm version is not supported by this VM (see <see cref="E7VM.Inspect"/>).</summary>
        Version = 14,
        /// <summary>The call waits for async host functions (<see cref="E7VM.RegisterAsyncNative(string, string, Func{object?[], Task{object?}})"/>).
        /// Reported by the synchronous Call / Load / Resume, which abandon such a call: use CallAsync / LoadAsync.</summary>
        Pending = 15,
    }

    /// <summary>Header of a module, read without loading it (<see cref="E7VM.Inspect"/>). Mirrors e7_module_info.</summary>
    public sealed class E7ModuleInfo
    {
        /// <summary>Ok: this VM can load the module; Version: format or required e7vm too new; Load: not a module.</summary>
        public E7Status Status { get; }
        /// <summary>Bytecode format version (this VM: 2).</summary>
        public uint Format { get; }
        /// <summary>Oldest e7vm that can run the module: major &lt;&lt; 16 | minor &lt;&lt; 8 | patch.</summary>
        public uint RequiresVersion { get; }
        /// <summary>An Ed25519 signature trailer is present (not verified by Inspect).</summary>
        public bool IsSigned { get; }
        /// <summary>First 8 bytes of SHA-256(public key) of the signing key (zero when unsigned).</summary>
        public byte[] KeyId { get; }
        public bool HasDebugInfo { get; }
        public bool CanRun => Status == E7Status.Ok;
        /// <summary>RequiresVersion as text, e.g. "0.1.0".</summary>
        public string RequiresVersionString => E7VM.VersionString(RequiresVersion);

        internal E7ModuleInfo(E7Status status, uint format, uint requires, bool signed, byte[] keyId, bool debug)
        {
            Status = status; Format = format; RequiresVersion = requires; IsSigned = signed; KeyId = keyId; HasDebugInfo = debug;
        }
    }

    /// <summary>Garbage collector statistics since the VM was created (<see cref="E7VM.GcStats"/>). Mirrors e7_gc_stats.</summary>
    public readonly struct E7GcStats
    {
        public ulong Collections { get; }
        public ulong TotalPauseNanoseconds { get; }
        /// <summary>Longest single collection.</summary>
        public ulong MaxPauseNanoseconds { get; }
        /// <summary>Currently allocated (same as <see cref="E7VM.MemoryUsed"/>).</summary>
        public long HeapBytes { get; }
        public long PeakHeapBytes { get; }

        internal E7GcStats(ulong collections, ulong total, ulong max, long heap, long peak)
        {
            Collections = collections; TotalPauseNanoseconds = total; MaxPauseNanoseconds = max; HeapBytes = heap; PeakHeapBytes = peak;
        }

        public override string ToString() =>
            $"E7GcStats(Collections={Collections}, TotalPauseNanoseconds={TotalPauseNanoseconds}, MaxPauseNanoseconds={MaxPauseNanoseconds}, HeapBytes={HeapBytes}, PeakHeapBytes={PeakHeapBytes})";
    }

    public sealed class E7Exception : Exception
    {
        public E7Status Status { get; }
        public bool IsSuspended => Status == E7Status.Suspended;
        public bool IsPending => Status == E7Status.Pending;
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

        // e7_config: must match include/e7/e7.h field by field (e7_config_init sets StructSize)
        [StructLayout(LayoutKind.Sequential)]
        private struct Config
        {
            public UIntPtr StructSize;
            public UIntPtr MemoryLimit;
            public uint StackSlots;
            public uint MaxFrames;
            public int SuspendOnBudget;
            public int RequireSignature;
            public IntPtr TrustedKeys;
            public UIntPtr NumTrustedKeys;
            public uint GcGrowthPercent;
        }

        // e7_module_info
        [StructLayout(LayoutKind.Sequential)]
        private struct ModuleInfo
        {
            public UIntPtr StructSize;
            public uint Format;
            public uint RequiresVersion;
            public int IsSigned;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public byte[] KeyId;
            public int HasDebugInfo;
        }

        // e7_gc_stats
        [StructLayout(LayoutKind.Sequential)]
        private struct GcStatsNative
        {
            public UIntPtr StructSize;
            public ulong Collections;
            public ulong TotalPauseNs;
            public ulong MaxPauseNs;
            public UIntPtr HeapBytes;
            public UIntPtr PeakHeapBytes;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeFn(IntPtr vm, IntPtr args, UIntPtr nargs, IntPtr ret, IntPtr user);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int AsyncNativeFn(IntPtr vm, IntPtr args, UIntPtr nargs, long ticket, IntPtr user);
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
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_register_async_native(IntPtr vm, byte[] name, byte[] signature, AsyncNativeFn fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_settle(IntPtr vm, long ticket, ref Value value);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_settle_error(IntPtr vm, long ticket, byte[] message);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern UIntPtr e7_open_tickets(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_set_print(IntPtr vm, PrintFn? fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_set_key_provider(IntPtr vm, KeyFn? fn, IntPtr user);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_seed_random(IntPtr vm, ulong seed);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern UIntPtr e7_memory_used(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_collect_garbage(IntPtr vm);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_sign_module(byte[] e7b, UIntPtr len, byte[] seed, out IntPtr output, out UIntPtr outLen);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void e7_public_key(byte[] seed, byte[] publicKey);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern uint e7_version_number();
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_inspect(byte[] data, UIntPtr len, ref ModuleInfo info);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_gc_stats_get(IntPtr vm, ref GcStatsNative stats);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_sign_detached(byte[] data, UIntPtr len, byte[] seed, byte[] sig);
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int e7_verify_detached(byte[] data, UIntPtr len, byte[] sig, byte[] trustedKeys, UIntPtr numTrustedKeys);

        // ------------------------------------------------------------------ state
        private IntPtr _vm;
        private readonly List<NativeBox> _natives = new List<NativeBox>();
        private readonly List<AsyncNativeBox> _asyncNatives = new List<AsyncNativeBox>();
        private AsyncRun? _currentRun;  // the CallAsync / LoadAsync in progress
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

        private sealed class AsyncNativeBox
        {
            public AsyncNativeFn Thunk = null!;
            public Func<object?[], CancellationToken, Task<object?>> Fn = null!;
            public string Name = "";
            public char ReturnCode;
        }

        // the outcome of a host operation, handed to the VM's context
        private sealed class Completion
        {
            public AsyncNativeBox Box = null!;
            public long Ticket;
            public Task<object?> Task = null!;
        }

        // one CallAsync / LoadAsync: the completions of its host operations, in the order they complete
        private sealed class AsyncRun : IDisposable
        {
            private readonly ConcurrentQueue<Completion> _queue = new ConcurrentQueue<Completion>();
            private readonly SemaphoreSlim _ready = new SemaphoreSlim(0);
            public readonly CancellationTokenSource Cancel;
            public IntPtr Module;  // LoadAsync

            public AsyncRun(CancellationToken token) { Cancel = CancellationTokenSource.CreateLinkedTokenSource(token); }

            public void Post(Completion c)
            {
                _queue.Enqueue(c);
                try { _ready.Release(); } catch (ObjectDisposedException) { }  // the call is over
            }

            public async Task<Completion> NextAsync()
            {
                await _ready.WaitAsync(Cancel.Token);  // continues on the caller's context (the VM's thread)
                _queue.TryDequeue(out var c);
                return c!;
            }

            public Completion Next()  // CallAwait / LoadAwait: blocks the calling thread
            {
                _ready.Wait(Cancel.Token);
                _queue.TryDequeue(out var c);
                return c!;
            }

            public void Dispose()
            {
                try { Cancel.Cancel(); } catch (ObjectDisposedException) { }  // host operations still running
                Cancel.Dispose();
                _ready.Dispose();
            }
        }

        /// <summary>Creates a VM. With trusted keys, only modules signed by one of them can be loaded.</summary>
        /// <param name="memoryLimit">heap limit in bytes (0: default 256 MiB)</param>
        /// <param name="suspendOnBudget">budget exhaustion suspends (resume with <see cref="Resume"/>) instead of failing</param>
        /// <param name="trustedKeys">Ed25519 public keys (32 bytes each); empty: signatures are not required</param>
        public E7VM(long memoryLimit = 0, bool suspendOnBudget = false, params byte[][] trustedKeys)
            : this(memoryLimit, suspendOnBudget, 0, trustedKeys)
        {
        }

        /// <summary>Creates a VM with a garbage collector growth setting.</summary>
        /// <param name="memoryLimit">heap limit in bytes (0: default 256 MiB)</param>
        /// <param name="suspendOnBudget">budget exhaustion suspends (resume with <see cref="Resume"/>) instead of failing</param>
        /// <param name="trustedKeys">Ed25519 public keys (32 bytes each); empty: signatures are not required</param>
        /// <param name="gcGrowthPercent">next collection when the heap reaches this % of the live size after the
        /// previous one (default 200, minimum 125; lower = less memory, more collections); 0 = default</param>
        public E7VM(long memoryLimit, bool suspendOnBudget, uint gcGrowthPercent, params byte[][] trustedKeys)
        {
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("E7VM requires a 64-bit process");
            var cfg = new Config();
            e7_config_init(ref cfg);
            if (memoryLimit > 0) cfg.MemoryLimit = (UIntPtr)(ulong)memoryLimit;
            cfg.SuspendOnBudget = suspendOnBudget ? 1 : 0;
            if (gcGrowthPercent > 0) cfg.GcGrowthPercent = gcGrowthPercent;
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

        /// <summary>Library version as major &lt;&lt; 16 | minor &lt;&lt; 8 | patch (compare with <see cref="E7ModuleInfo.RequiresVersion"/>).</summary>
        public static uint VersionNumber => e7_version_number();

        internal static string VersionString(uint v) => $"{v >> 16}.{(v >> 8) & 0xFF}.{v & 0xFF}";

        /// <summary>Reads a module's header without loading it, e.g. to skip modules this VM cannot run.</summary>
        public static E7ModuleInfo Inspect(byte[] bytecode)
        {
            if (bytecode == null) throw new ArgumentNullException(nameof(bytecode));
            var info = new ModuleInfo { StructSize = (UIntPtr)(uint)Marshal.SizeOf<ModuleInfo>(), KeyId = new byte[8] };
            int st = e7_inspect(bytecode, (UIntPtr)(uint)bytecode.Length, ref info);
            return new E7ModuleInfo((E7Status)st, info.Format, info.RequiresVersion, info.IsSigned != 0, info.KeyId ?? new byte[8], info.HasDebugInfo != 0);
        }

        public void Dispose()
        {
            if (_vm != IntPtr.Zero)
            {
                var run = _currentRun;
                _currentRun = null;
                e7_vm_free(_vm);
                _vm = IntPtr.Zero;
                try { run?.Cancel.Cancel(); } catch (ObjectDisposedException) { }  // its CallAsync throws ObjectDisposedException
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

        // A synchronous call that returned E7_PENDING: nothing would settle its tickets, so it is abandoned.
        private void CheckSync(int status, string what)
        {
            if (status == (int)E7Status.Pending)
            {
                e7_abort_suspended(_vm);
                throw new E7Exception(E7Status.Pending,
                    $"{what} waits for an async host function, so it was abandoned: run it with CallAsync / LoadAsync");
            }
            Check(status);
        }

        // ------------------------------------------------------------------ loading and calling
        /// <summary>Verifies (signature if required, structure, types), links natives and runs the module's top-level code.
        /// Top-level code that awaits an async host function throws E7Exception (Pending; the initializer is abandoned):
        /// load such a module with <see cref="LoadAsync"/>.</summary>
        public E7Module Load(byte[] bytecode)
        {
            if (bytecode == null) throw new ArgumentNullException(nameof(bytecode));
            CheckSync(e7_load(Handle, bytecode, (UIntPtr)(uint)bytecode.Length, out var m), "the module's top-level code");
            return new E7Module(m);
        }

        /// <summary>Compiles and loads source (development; needs the compiler in the library).</summary>
        public E7Module LoadSource(string source, string fileName = "main.ts")
        {
            var src = Encoding.UTF8.GetBytes(source);
            CheckSync(e7_load_source(Handle, src, (UIntPtr)(uint)src.Length, Z(fileName), out var m), "the module's top-level code");
            return new E7Module(m);
        }

        public bool HasFunction(E7Module module, string function) => e7_has_function(module.Ptr, Z(function)) != 0;

        /// <summary>Calls an exported function. Throws <see cref="E7Exception"/> on errors (IsSuspended when the budget suspends;
        /// IsPending when the call waits for an async host function: it is abandoned, use <see cref="CallAsync(E7Module, string, object?[])"/>).
        /// An async export returns the promise's value when no host operation is left open.</summary>
        public object? Call(E7Module module, string function, params object?[] args)
        {
            if (module == null) throw new ArgumentNullException(nameof(module));
            args ??= new object?[] { null };
            var buffers = new List<IntPtr>();
            try
            {
                var vals = new Value[Math.Max(args.Length, 1)];
                for (int i = 0; i < args.Length; i++) vals[i] = ToNative(args[i], buffers);
                CheckSync(e7_call(Handle, module.Ptr, Z(function), vals, (UIntPtr)(uint)args.Length, out var ret), $"'{function}'");
                return FromNative(ret);
            }
            finally
            {
                FreeAll(buffers);
            }
        }

        public object? Resume()
        {
            CheckSync(e7_resume(Handle, out var ret), "the call");
            return FromNative(ret);
        }

        /// <summary>Abandons a suspended call, or the CallAsync / LoadAsync in progress (it throws OperationCanceledException).</summary>
        public void AbortSuspended()
        {
            var vm = Handle;
            var run = _currentRun;
            _currentRun = null;
            e7_abort_suspended(vm);
            run?.Cancel.Cancel();
        }

        // ------------------------------------------------------------------ async host functions
        /// <summary>Number of async host operations the VM waits for (e7_open_tickets).</summary>
        public long OpenTickets => (long)(ulong)e7_open_tickets(Handle);

        /// <summary>
        /// Registers an async host function before <see cref="Load"/>. The script declares it returning a promise, e.g.
        /// <c>declare function fetchKey(id: string): Promise&lt;Uint8Array&gt;;</c> with signature "s&gt;y" (the return code is
        /// the promise's value type). <paramref name="fn"/> runs on the VM's thread when the script calls the function;
        /// its task may complete on any thread. A task that is already complete settles the promise at once (then even
        /// <see cref="Call"/> works); otherwise its completion goes to the <see cref="CallAsync(E7Module, string, object?[])"/>
        /// in progress, which settles the promises on the VM's context in the order the operations complete. A faulted
        /// task rejects the promise with an Error carrying the exception's message.
        /// </summary>
        public void RegisterAsyncNative(string name, string signature, Func<object?[], Task<object?>> fn)
        {
            if (fn == null) throw new ArgumentNullException(nameof(fn));
            RegisterAsyncNative(name, signature, (args, _) => fn(args));
        }

        /// <summary><see cref="RegisterAsyncNative(string, string, Func{object?[], Task{object?}})"/> with a token that is
        /// cancelled when the call that started the operation finishes or is abandoned.</summary>
        public void RegisterAsyncNative(string name, string signature, Func<object?[], CancellationToken, Task<object?>> fn)
        {
            if (fn == null) throw new ArgumentNullException(nameof(fn));
            int gt = signature.IndexOf('>');
            if (gt < 0 || gt + 1 >= signature.Length) throw new ArgumentException("signature must look like \"s>y\"", nameof(signature));
            var box = new AsyncNativeBox { Fn = fn, Name = name, ReturnCode = signature[gt + 1] };
            box.Thunk = (vm, args, nargs, ticket, user) =>
            {
                Task<object?> task;
                var run = _currentRun;
                try
                {
                    int n = (int)(ulong)nargs;
                    var vals = new object?[n];
                    int size = Marshal.SizeOf<Value>();
                    for (int i = 0; i < n; i++) vals[i] = FromNative(Marshal.PtrToStructure<Value>(args + i * size));
                    task = box.Fn(vals, run?.Cancel.Token ?? CancellationToken.None)
                           ?? throw new InvalidOperationException($"{name} returned null instead of a Task");
                }
                catch (Exception e)
                {
                    e7_native_error(vm, Z(e.Message));
                    return (int)E7Status.Runtime;
                }
                var c = new Completion { Box = box, Ticket = ticket, Task = task };
                if (task.IsCompleted) Settle(c);  // settled before the script goes on
                else if (run != null) task.ContinueWith(_ => run.Post(c), CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                // else: a synchronous Call, which abandons the call when it returns E7_PENDING
                return 0;
            };
            _asyncNatives.Add(box);  // keeps the delegate alive as long as the VM
            Check(e7_register_async_native(Handle, Z(name), Z(signature), box.Thunk, IntPtr.Zero));
        }

        /// <summary>
        /// Calls an exported function that may wait for async host functions (for an async export, the result is the
        /// promise's value). The VM runs on the caller's context: the call starts synchronously, and each completion is
        /// settled where the awaits continue (the caller's SynchronizationContext, e.g. the UI thread; otherwise one
        /// thread-pool thread at a time). Use the VM from that context only while the call is in progress; other calls
        /// fail with E7Exception (State) meanwhile.
        /// </summary>
        public Task<object?> CallAsync(E7Module module, string function, params object?[] args) =>
            CallAsync(module, function, args, CancellationToken.None);

        /// <summary><see cref="CallAsync(E7Module, string, object?[])"/>; cancelling the token abandons the call
        /// (OperationCanceledException).</summary>
        public async Task<object?> CallAsync(E7Module module, string function, object?[] args, CancellationToken cancellationToken)
        {
            if (module == null) throw new ArgumentNullException(nameof(module));
            var run = Begin(cancellationToken);
            var (st, result) = StartCall(run, module, function, args);
            return await Drive(run, st, result);
        }

        /// <summary><see cref="Load"/> for a module whose top-level code awaits async host functions (see
        /// <see cref="CallAsync(E7Module, string, object?[])"/>).</summary>
        public async Task<E7Module> LoadAsync(byte[] bytecode, CancellationToken cancellationToken = default)
        {
            if (bytecode == null) throw new ArgumentNullException(nameof(bytecode));
            var run = Begin(cancellationToken);
            int st = e7_load(_vm, bytecode, (UIntPtr)(uint)bytecode.Length, out var m);
            run.Module = m;
            await Drive(run, st, null);
            return new E7Module(run.Module);
        }

        /// <summary>
        /// Calls an exported function and blocks until it finishes: the calling thread is the VM's thread and settles
        /// the async host functions' results in the order they complete (as Java's callAwait). Host functions start
        /// without a SynchronizationContext here, so an <c>async</c> host function does not wait for the blocked
        /// thread. Throws <see cref="E7Exception"/> like <see cref="Call"/>.
        /// </summary>
        public object? CallAwait(E7Module module, string function, params object?[] args)
        {
            if (module == null) throw new ArgumentNullException(nameof(module));
            var run = Begin(CancellationToken.None);
            return Blocking(run, () => StartCall(run, module, function, args));
        }

        /// <summary><see cref="Load"/> for a module whose top-level code awaits async host functions, blocking
        /// like <see cref="CallAwait"/>.</summary>
        public E7Module LoadAwait(byte[] bytecode)
        {
            if (bytecode == null) throw new ArgumentNullException(nameof(bytecode));
            var run = Begin(CancellationToken.None);
            Blocking(run, () =>
            {
                int st = e7_load(_vm, bytecode, (UIntPtr)(uint)bytecode.Length, out var m);
                run.Module = m;
                return (st, null);
            });
            return new E7Module(run.Module);
        }

        private AsyncRun Begin(CancellationToken token)
        {
            var vm = Handle;
            if (_currentRun != null)
                throw new E7Exception(E7Status.State, "VM waits for host operations of another call; finish or cancel it first");
            token.ThrowIfCancellationRequested();
            var run = new AsyncRun(token);
            _currentRun = run;
            return run;
        }

        private void End(AsyncRun run)
        {
            if (_currentRun == run) _currentRun = null;
            run.Dispose();
        }

        // the first step of an async call: e7_call (status, and the result when E7_OK)
        private (int, object?) StartCall(AsyncRun run, E7Module module, string function, object?[] args)
        {
            args ??= new object?[] { null };
            var buffers = new List<IntPtr>();
            try
            {
                var vals = new Value[Math.Max(args.Length, 1)];
                for (int i = 0; i < args.Length; i++) vals[i] = ToNative(args[i], buffers);
                int st = e7_call(_vm, module.Ptr, Z(function), vals, (UIntPtr)(uint)args.Length, out var ret);
                return (st, st == 0 ? FromNative(ret) : null);
            }
            catch
            {
                End(run);
                throw;
            }
            finally
            {
                FreeAll(buffers);
            }
        }

        // E7_PENDING: settle completions in the order they arrive and resume, until the call finishes
        private async Task<object?> Drive(AsyncRun run, int st, object? result)
        {
            try
            {
                while (st == (int)E7Status.Pending)
                {
                    Completion c;
                    try
                    {
                        c = await run.NextAsync();
                    }
                    catch (OperationCanceledException)
                    {
                        Abandoned(run);
                        throw;
                    }
                    Advance(c, ref st, ref result);
                }
                return Finish(st, result);
            }
            finally
            {
                End(run);
            }
        }

        // the blocking driver of CallAwait / LoadAwait
        private object? Blocking(AsyncRun run, Func<(int, object?)> first)
        {
            var context = SynchronizationContext.Current;
            SynchronizationContext.SetSynchronizationContext(null);
            try
            {
                var (st, result) = first();
                while (st == (int)E7Status.Pending)
                {
                    Completion c;
                    try
                    {
                        c = run.Next();
                    }
                    catch (OperationCanceledException)
                    {
                        Abandoned(run);
                        throw;
                    }
                    Advance(c, ref st, ref result);
                }
                return Finish(st, result);
            }
            finally
            {
                End(run);
                SynchronizationContext.SetSynchronizationContext(context);
            }
        }

        // the call was cancelled, abandoned (AbortSuspended) or the VM disposed
        private void Abandoned(AsyncRun run)
        {
            if (_vm == IntPtr.Zero) throw new ObjectDisposedException(nameof(E7VM));
            if (_currentRun == run)
            {
                _currentRun = null;
                e7_abort_suspended(_vm);
            }
        }

        // a host operation completed: settle its ticket and let the script continue
        private void Advance(Completion c, ref int st, ref object? result)
        {
            if (_vm == IntPtr.Zero) throw new ObjectDisposedException(nameof(E7VM));
            if (!Settle(c)) return;  // not open any more
            st = e7_resume(_vm, out var ret);
            if (st == 0) result = FromNative(ret);
        }

        private object? Finish(int st, object? result)
        {
            if (st == (int)E7Status.Suspended)
            {  // an async call does not stop for the budget: abandon it
                e7_abort_suspended(_vm);
                throw new E7Exception(E7Status.Suspended, "instruction budget exhausted (the async call was abandoned)");
            }
            Check(st);
            return result;
        }

        // Settles a ticket on the VM's thread; false if it is not open. A value of the wrong type rejects the promise.
        private bool Settle(Completion c)
        {
            var t = c.Task;
            var box = c.Box;
            int st;
            if (t.Status == TaskStatus.RanToCompletion)
            {
                var buffers = new List<IntPtr>();
                try
                {
                    var v = box.ReturnCode == 'v' ? new Value { Type = ValueType.Void } : ToNative(Coerce(t.Result, box.ReturnCode), buffers);
                    st = e7_settle(_vm, c.Ticket, ref v);
                    if (st == (int)E7Status.Args)
                        st = e7_settle_error(_vm, c.Ticket, Z($"{box.Name}: {PtrToUtf8(e7_last_error(_vm), -1)}"));
                }
                catch (Exception e)  // Coerce / ToNative: a value of the wrong type
                {
                    st = e7_settle_error(_vm, c.Ticket, Z($"{box.Name}: {e.Message}"));
                }
                finally
                {
                    FreeAll(buffers);
                }
            }
            else
            {
                var agg = t.Exception;
                Exception? e = agg == null ? null : agg.InnerExceptions.Count == 1 ? agg.InnerExceptions[0] : agg;
                st = e7_settle_error(_vm, c.Ticket, Z(t.IsCanceled ? $"{box.Name}: cancelled" : e?.Message ?? "failed"));
            }
            return st == 0;
        }

        /// <summary>Instruction budget for subsequent execution (&lt;= 0: unlimited).</summary>
        public void SetBudget(long ticks) => e7_set_budget(Handle, ticks);
        public void SeedRandom(ulong seed) => e7_seed_random(Handle, seed);
        public long MemoryUsed => (long)(ulong)e7_memory_used(Handle);
        public void CollectGarbage() => e7_collect_garbage(Handle);

        /// <summary>Garbage collector statistics (count, pause times, heap size and peak), e.g. for sizing the memory limit.</summary>
        public E7GcStats GcStats()
        {
            var st = new GcStatsNative { StructSize = (UIntPtr)(uint)Marshal.SizeOf<GcStatsNative>() };
            Check(e7_gc_stats_get(Handle, ref st));
            return new E7GcStats(st.Collections, st.TotalPauseNs, st.MaxPauseNs, (long)(ulong)st.HeapBytes, (long)(ulong)st.PeakHeapBytes);
        }

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

        /// <summary>Detached Ed25519 signature (64 bytes) of arbitrary data, e.g. an update manifest (e7_sign_detached).</summary>
        public static byte[] SignDetached(byte[] data, byte[] seed)
        {
            if (data == null) throw new ArgumentNullException(nameof(data));
            if (seed == null || seed.Length != 32) throw new ArgumentException("seed must be 32 bytes", nameof(seed));
            var sig = new byte[64];
            int st = e7_sign_detached(data, (UIntPtr)(uint)data.Length, seed, sig);
            if (st != 0) throw new E7Exception((E7Status)st, "signing failed");
            return sig;
        }

        /// <summary>True if <paramref name="signature"/> is a detached signature of <paramref name="data"/> by one of the public keys.</summary>
        public static bool VerifyDetached(byte[] data, byte[] signature, params byte[][] trustedKeys)
        {
            if (data == null) throw new ArgumentNullException(nameof(data));
            if (signature == null || signature.Length != 64 || trustedKeys == null || trustedKeys.Length == 0) return false;
            var keys = new byte[32 * trustedKeys.Length];
            for (int i = 0; i < trustedKeys.Length; i++)
            {
                if (trustedKeys[i] == null || trustedKeys[i].Length != 32)
                    throw new ArgumentException("public keys must be 32 bytes", nameof(trustedKeys));
                Buffer.BlockCopy(trustedKeys[i], 0, keys, 32 * i, 32);
            }
            return e7_verify_detached(data, (UIntPtr)(uint)data.Length, signature, keys, (UIntPtr)(uint)trustedKeys.Length) == 0;
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
