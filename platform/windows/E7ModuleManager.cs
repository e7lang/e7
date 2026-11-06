// Over-the-air delivery of E7 modules: signed manifest, staged rollout, kill switch, last-known-good and
// rollback (.NET). Same behaviour as org.e7vm.E7ModuleManager (Java / Kotlin) and E7ModuleManager.swift;
// see docs/DEPLOYMENT.md.
//
//   using var mm = new E7.E7ModuleManager(new E7.E7ModuleManager.Options
//   {
//       ManifestUrl = new Uri("https://updates.example.com/pricing/manifest.json"),
//       TrustedKeys = { releaseKey, nextReleaseKey },          // Ed25519 public keys (32 bytes)
//       Fetcher = url => http.GetByteArrayAsync(url).GetAwaiter().GetResult(),   // called on Update's thread
//       StorageDirectory = Path.Combine(appData, "e7", "pricing"),
//       UserId = installationId,                               // stable per user / install
//       FallbackModule = bundledModule, FallbackVersion = "bundled-1.4",
//       MemoryLimit = 16 << 20,
//       SetUpVM = vm => { vm.SetBudget(5_000_000); vm.RegisterNative(...); },
//       ReportError = (version, status, message) => telemetry.Log(version, status, message),
//   });
//   mm.Start();                                      // at launch: last-known-good from disk, or the fallback
//   await Task.Run(() => mm.Update());               // manifest, download, verify, self-test, activate
//   var price = mm.Active!.Call("price", order);
//   var quote = await mm.Active!.CallAsync("quote", order);   // an export that awaits async host functions
//   mm.ReportSuccess();                              // or mm.ReportFailure(status, message)
//
// The manifest is JSON {version, url, sha256, rolloutPercent, killSwitch, minVm?, serial?} with a detached
// signature (hex, `e7c --sign-file` / tools/manifest.py) at "<manifest URL>.sig". Manifests that do not verify
// against the trusted keys are ignored, so a kill switch or a rollout cannot be forged. Downloaded modules must be
// signed by a trusted key too; the bundled fallback is not checked (it is part of the app).
//
// The manager owns its VMs (a replaced VM is disposed). Its methods are serialized by a lock; like a VM, do not
// call into the active module while Update() may replace it on another thread. Module initializers and selfTest
// may await async host functions registered in SetUpVM: the manager waits for them (E7VM.LoadAwait / CallAwait)
// on the thread that calls Start() / Update().
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace E7
{
    public sealed class E7ModuleManager : IDisposable
    {
        /// <summary>Configuration; ManifestUrl, TrustedKeys, Fetcher, StorageDirectory, UserId and FallbackModule are required.</summary>
        public sealed class Options
        {
            public Uri? ManifestUrl { get; set; }
            /// <summary>Ed25519 public keys (32 bytes each) trusted for manifests and modules; several during a key rotation.</summary>
            public List<byte[]> TrustedKeys { get; set; } = new List<byte[]>();
            /// <summary>Downloads a URL (manifest, signature, module) with the app's own HTTP stack; called synchronously.</summary>
            public Func<Uri, byte[]>? Fetcher { get; set; }
            /// <summary>Private directory for the last-known-good modules and the manager's state.</summary>
            public string? StorageDirectory { get; set; }
            /// <summary>Stable id of the user or installation; decides the rollout bucket.</summary>
            public string? UserId { get; set; }
            /// <summary>The module bundled with the app: used on a kill switch or when no downloaded module is usable.</summary>
            public byte[]? FallbackModule { get; set; }
            public string FallbackVersion { get; set; } = "fallback";
            /// <summary>Heap limit of the VMs in bytes (0: default).</summary>
            public long MemoryLimit { get; set; }
            /// <summary>Garbage collector growth setting of the VMs (0: default).</summary>
            public uint GcGrowthPercent { get; set; }
            /// <summary>Prepares every new VM before the module loads: natives, key provider, print handler, budget.</summary>
            public Action<E7VM>? SetUpVM { get; set; }
            /// <summary>Problems the manager handled: (module version or null if unknown, status, message).</summary>
            public Action<string?, E7Status, string>? ReportError { get; set; }
            /// <summary>Consecutive failures (ReportFailure) after which a downloaded module is rolled back.</summary>
            public int MaxConsecutiveFailures { get; set; } = 3;
            /// <summary>Exported function that must return true before a downloaded module is activated (null: none).</summary>
            public string? SelfTestFunction { get; set; } = "selfTest";
        }

        /// <summary>The module currently in use.</summary>
        public sealed class ActiveModule
        {
            public E7VM Vm { get; }
            public E7Module Module { get; }
            /// <summary>Version from the manifest, or the fallback's version label.</summary>
            public string Version { get; }
            /// <summary>Hex SHA-256 of the module file; null for the fallback.</summary>
            public string? Sha256 { get; }
            public bool IsFallback { get; }

            internal ActiveModule(E7VM vm, E7Module module, string version, string? sha256, bool isFallback)
            {
                Vm = vm; Module = module; Version = version; Sha256 = sha256; IsFallback = isFallback;
            }

            /// <summary>Calls an exported function of the active module.</summary>
            public object? Call(string function, params object?[] args) => Vm.Call(Module, function, args);

            /// <summary><see cref="E7VM.CallAsync(E7Module, string, object?[])"/> on the active module (exports that await async host functions).</summary>
            public Task<object?> CallAsync(string function, params object?[] args) => Vm.CallAsync(Module, function, args);

            /// <summary><see cref="E7VM.CallAwait"/> on the active module (blocks until the export's promise settles).</summary>
            public object? CallAwait(string function, params object?[] args) => Vm.CallAwait(Module, function, args);
        }

        private sealed class Entry
        {
            public readonly string Version, Sha256;
            public Entry(string version, string sha256) { Version = version; Sha256 = sha256; }
        }

        private sealed class Manifest
        {
            public string? Version;
            public string Url = "", Sha256 = "";
            public double RolloutPercent;
            public bool KillSwitch;
            public uint MinVm;
            public long? Serial;
        }

        private sealed class Failure : Exception
        {
            public E7Status Status { get; }
            public Failure(E7Status status, string message) : base(message) { Status = status; }
        }

        private readonly object _gate = new object();
        private readonly Uri _manifestUrl;
        private readonly byte[][] _trustedKeys;
        private readonly Func<Uri, byte[]> _fetcher;
        private readonly string _userId;
        private readonly byte[] _fallbackModule;
        private readonly string _fallbackVersion;
        private readonly long _memoryLimit;
        private readonly uint _gcGrowthPercent;
        private readonly Action<E7VM>? _setUpVM;
        private readonly Action<string?, E7Status, string>? _reportError;
        private readonly int _maxFailures;
        private readonly string? _selfTestFunction;
        private readonly string _dir, _modulesDir, _stateFile;

        // persistent state (state.json, the same layout as the Swift manager's)
        private Entry? _current, _previous;  // last-known-good and the one before it
        private int _failures;
        private bool _killed;
        private long? _serial;  // newest manifest serial accepted
        private readonly List<string> _bad = new List<string>();  // sha256 of rolled-back / broken modules

        private ActiveModule? _active;

        public E7ModuleManager(Options options)
        {
            if (options == null) throw new ArgumentNullException(nameof(options));
            if (options.ManifestUrl == null || options.Fetcher == null || options.StorageDirectory == null || options.UserId == null
                || options.FallbackModule == null)
                throw new ArgumentException("ManifestUrl, Fetcher, StorageDirectory, UserId and FallbackModule are required", nameof(options));
            if (options.TrustedKeys == null || options.TrustedKeys.Count == 0)
                throw new ArgumentException("at least one trusted key is required", nameof(options));
            foreach (var k in options.TrustedKeys)
                if (k == null || k.Length != 32) throw new ArgumentException("public keys must be 32 bytes", nameof(options));
            if (options.MaxConsecutiveFailures < 1) throw new ArgumentException("MaxConsecutiveFailures must be at least 1", nameof(options));
            _manifestUrl = options.ManifestUrl;
            _trustedKeys = options.TrustedKeys.ToArray();
            _fetcher = options.Fetcher;
            _userId = options.UserId;
            _fallbackModule = options.FallbackModule;
            _fallbackVersion = options.FallbackVersion;
            _memoryLimit = options.MemoryLimit;
            _gcGrowthPercent = options.GcGrowthPercent;
            _setUpVM = options.SetUpVM;
            _reportError = options.ReportError;
            _maxFailures = options.MaxConsecutiveFailures;
            _selfTestFunction = options.SelfTestFunction;
            _dir = options.StorageDirectory;
            _modulesDir = Path.Combine(_dir, "modules");
            _stateFile = Path.Combine(_dir, "state.json");
            LoadState();
        }

        /// <summary>The module in use (null before <see cref="Start"/> / <see cref="Update"/>).</summary>
        public ActiveModule? Active
        {
            get { lock (_gate) return _active; }
        }

        /// <summary>
        /// Activates the last-known-good module from disk without network access (or the fallback when there is none,
        /// it no longer loads, or a kill switch is in effect). Call at app start. Throws only if the bundled fallback
        /// module does not load.
        /// </summary>
        public ActiveModule Start()
        {
            lock (_gate)
            {
                ActivateFromDisk();
                return _active!;
            }
        }

        /// <summary>
        /// Fetches and verifies the manifest; downloads, verifies, self-tests and activates the module it names when
        /// this client is in the rollout. On any problem the current module stays active (the problem goes to
        /// ReportError). Blocks while downloading: call it from a background thread. Throws only if the bundled
        /// fallback module does not load.
        /// </summary>
        public ActiveModule Update()
        {
            lock (_gate)
            {
                if (_active == null) ActivateFromDisk();
                Manifest mf;
                try
                {
                    var body = _fetcher(_manifestUrl);
                    var sig = ParseSignature(_fetcher(new Uri(_manifestUrl.AbsoluteUri + ".sig")));
                    if (body == null || sig == null || !E7VM.VerifyDetached(body, sig, _trustedKeys))
                    {
                        Report(null, E7Status.Signature, "manifest signature is not valid for the trusted keys");
                        return _active!;
                    }
                    mf = ParseManifest(body);
                }
                catch (Exception e)
                {
                    Report(null, E7Status.Load, "manifest: " + Describe(e));
                    return _active!;
                }
                // replay protection: once a manifest with a serial was accepted, older (or serial-less) ones are ignored
                if (_serial.HasValue && (!mf.Serial.HasValue || mf.Serial.Value < _serial.Value))
                {
                    Report(mf.Version, E7Status.Signature,
                        $"stale manifest ignored (serial {(mf.Serial.HasValue ? mf.Serial.Value.ToString(CultureInfo.InvariantCulture) : "missing")}, newest accepted {_serial.Value})");
                    return _active!;
                }
                if (mf.Serial.HasValue) _serial = mf.Serial;
                _killed = mf.KillSwitch;
                SaveState();
                if (mf.KillSwitch)
                {
                    if (!_active!.IsFallback) ActivateFallback();
                    return _active!;
                }
                var version = mf.Version ?? "";
                if (RolloutBucket(_userId, version) >= (int)Math.Round(mf.RolloutPercent * 100, MidpointRounding.AwayFromZero))
                    return KeepCurrent();
                if (mf.MinVm > E7VM.VersionNumber)
                {
                    Report(version, E7Status.Version, $"module requires e7vm {E7VM.VersionString(mf.MinVm)}, this is {E7VM.Version}");
                    return KeepCurrent();
                }
                if (_bad.Contains(mf.Sha256)) return KeepCurrent();  // rolled back before: wait for a new version
                if (_active != null && !_active.IsFallback && _active.Sha256 == mf.Sha256) return _active;  // up to date
                var code = ReadCached(mf.Sha256);
                if (code == null)
                {
                    try
                    {
                        code = _fetcher(new Uri(_manifestUrl, mf.Url));
                    }
                    catch (Exception e)
                    {
                        Report(version, E7Status.Load, "download failed: " + Describe(e));
                        return KeepCurrent();
                    }
                    var got = code == null ? "nothing" : Sha256Hex(code);
                    if (got != mf.Sha256)
                    {
                        Report(version, E7Status.Signature, $"sha256 mismatch: manifest {mf.Sha256}, downloaded {got}");
                        return KeepCurrent();
                    }
                }
                var next = InstantiateChecked(code!, version, mf.Sha256);
                if (next == null) return KeepCurrent();
                try
                {
                    WriteAtomically(ModulePath(mf.Sha256), code!);
                }
                catch (Exception e)
                {
                    Report(version, E7Status.State, "cannot store the module: " + Describe(e));
                }
                if (_current != null && _current.Sha256 != mf.Sha256) _previous = _current;
                _current = new Entry(version, mf.Sha256);
                _failures = 0;
                SaveState();
                SwitchTo(next);
                return next;
            }
        }

        /// <summary>The host saw the active module work (e.g. a call returned a valid result): resets the failure count.</summary>
        public void ReportSuccess()
        {
            lock (_gate)
            {
                if (_failures == 0) return;
                _failures = 0;
                SaveState();
            }
        }

        /// <summary>
        /// The host saw the active module fail (an exception, a result it had to reject). After MaxConsecutiveFailures
        /// consecutive failures a downloaded module is rolled back to the previous good one, or the fallback, and is
        /// not activated again.
        /// </summary>
        public void ReportFailure(E7Status status, string message)
        {
            lock (_gate)
            {
                var a = _active;
                if (a == null || a.IsFallback || a.Sha256 == null) return;
                _failures++;
                if (_failures < _maxFailures)
                {
                    SaveState();
                    return;
                }
                Report(a.Version, status, $"rolled back after {_failures} consecutive failures: {message}");
                MarkBad(a.Sha256);
                _failures = 0;
                SaveState();
                ActivateFromDisk();
            }
        }

        /// <summary>Disposes the active VM.</summary>
        public void Dispose()
        {
            lock (_gate)
            {
                _active?.Vm.Dispose();
                _active = null;
            }
        }

        /// <summary>
        /// Rollout bucket 0..9999 of a user for a module version: the first 4 bytes of SHA-256(UTF-8 of
        /// version + ":" + userId) as a big-endian unsigned number, modulo 10000. A client takes the version when
        /// bucket &lt; rolloutPercent * 100.
        /// </summary>
        public static int RolloutBucket(string userId, string version)
        {
            var h = Sha256(Encoding.UTF8.GetBytes(version + ":" + userId));
            uint v = (uint)h[0] << 24 | (uint)h[1] << 16 | (uint)h[2] << 8 | h[3];
            return (int)(v % 10000);
        }

        // ------------------------------------------------------------------ activation
        private ActiveModule KeepCurrent()
        {
            if (_active == null || _active.IsFallback) ActivateFromDisk();
            return _active!;
        }

        // Last-known-good, then the previous module, then the fallback. Unusable entries are dropped.
        private void ActivateFromDisk()
        {
            if (!_killed)
            {
                for (int i = 0; i < 2; i++)
                {
                    var e = _current;
                    if (e == null) break;
                    if (_active != null && !_active.IsFallback && e.Sha256 == _active.Sha256) return;
                    ActiveModule? a = null;
                    if (!_bad.Contains(e.Sha256))
                    {
                        var code = ReadCached(e.Sha256);
                        if (code == null) Report(e.Version, E7Status.Load, "stored module is missing or corrupted");
                        else a = InstantiateChecked(code, e.Version, e.Sha256);
                        if (a == null) MarkBad(e.Sha256);
                    }
                    if (a != null)
                    {
                        SaveState();
                        SwitchTo(a);
                        return;
                    }
                    _current = _previous;  // drop the unusable module
                    _previous = null;
                    _failures = 0;
                    SaveState();
                }
            }
            if (_active == null || !_active.IsFallback) ActivateFallback();
        }

        private void ActivateFallback()
        {
            E7VM? vm = null;
            try
            {
                vm = NewVM(false);
                var m = vm.LoadAwait(_fallbackModule);  // top-level code may await async host functions
                SwitchTo(new ActiveModule(vm, m, _fallbackVersion, null, true));
            }
            catch (Exception e)
            {
                vm?.Dispose();
                var st = e is E7Exception x ? x.Status : E7Status.State;
                Report(_fallbackVersion, st, "the fallback module does not load: " + Describe(e));
                if (e is E7Exception) throw;
                throw new E7Exception(st, Describe(e));
            }
        }

        // inspect + signed load + selfTest; reports and returns null on failure
        private ActiveModule? InstantiateChecked(byte[] code, string version, string sha256)
        {
            var info = E7VM.Inspect(code);
            if (!info.CanRun)
            {
                Report(version, info.Status, info.Status == E7Status.Version
                    ? $"module needs e7vm {info.RequiresVersionString} (format {info.Format}), this is {E7VM.Version}"
                    : "not a valid e7 module");
                return null;
            }
            E7VM? vm = null;
            try
            {
                vm = NewVM(true);
                var m = vm.LoadAwait(code);  // top-level code and selfTest may await async host functions
                if (_selfTestFunction != null && vm.HasFunction(m, _selfTestFunction))
                {
                    var r = vm.CallAwait(m, _selfTestFunction);
                    if (!(r is bool ok && ok))
                        throw new E7Exception(E7Status.Runtime, $"{_selfTestFunction}() returned {Show(r)}");
                }
                return new ActiveModule(vm, m, version, sha256, false);
            }
            catch (Exception e)
            {
                vm?.Dispose();
                Report(version, e is E7Exception x ? x.Status : E7Status.Runtime, Describe(e));
                return null;
            }
        }

        private static string Show(object? v) => v switch
        {
            null => "null",
            bool b => b ? "true" : "false",
            IFormattable f => f.ToString(null, CultureInfo.InvariantCulture),
            _ => v.ToString() ?? "",
        };

        private E7VM NewVM(bool requireSignature)
        {
            var vm = new E7VM(_memoryLimit, false, _gcGrowthPercent, requireSignature ? _trustedKeys : new byte[0][]);
            try
            {
                _setUpVM?.Invoke(vm);
            }
            catch
            {
                vm.Dispose();
                throw;
            }
            return vm;
        }

        private void SwitchTo(ActiveModule next)
        {
            var old = _active;
            _active = next;
            if (old != null && old != next) old.Vm.Dispose();
            CleanModules();
        }

        private void MarkBad(string sha)
        {
            if (!_bad.Contains(sha)) _bad.Add(sha);
            if (_bad.Count > 32) _bad.RemoveRange(0, _bad.Count - 32);
            if (_current != null && _current.Sha256 == sha && _active != null && _active.Sha256 == sha)
            {
                _current = _previous;
                _previous = null;
            }
            else if (_previous != null && _previous.Sha256 == sha)
            {
                _previous = null;
            }
        }

        private void Report(string? version, E7Status status, string message)
        {
            try
            {
                _reportError?.Invoke(version, status, message);
            }
            catch
            {
                // a failing reporter must not break module management
            }
        }

        private static string Describe(Exception e) => string.IsNullOrEmpty(e.Message) ? e.GetType().Name : e.Message;

        // ------------------------------------------------------------------ storage
        private string ModulePath(string sha) => Path.Combine(_modulesDir, sha + ".e7b");

        private byte[]? ReadCached(string sha)
        {
            try
            {
                var p = ModulePath(sha);
                if (!File.Exists(p)) return null;
                var b = File.ReadAllBytes(p);
                return Sha256Hex(b) == sha ? b : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private void CleanModules()
        {
            if (!Directory.Exists(_modulesDir)) return;
            foreach (var f in Directory.GetFiles(_modulesDir))
            {
                var n = Path.GetFileName(f);
                bool keep = (_current != null && n == _current.Sha256 + ".e7b") || (_previous != null && n == _previous.Sha256 + ".e7b");
                if (!keep)
                {
                    try { File.Delete(f); } catch (Exception) { }
                }
            }
        }

        private void LoadState()
        {
            try
            {
                if (!File.Exists(_stateFile)) return;
                if (!(new Json(Encoding.UTF8.GetString(File.ReadAllBytes(_stateFile))).ParseDocument() is Dictionary<string, object?> o)) return;
                _current = ReadEntry(o, "current");
                _previous = ReadEntry(o, "previous");
                _failures = o.TryGetValue("failures", out var f) && f is double fd ? (int)fd : 0;
                _killed = o.TryGetValue("killed", out var k) && k is bool kb && kb;
                _serial = o.TryGetValue("serial", out var s) && s is double sd ? (long)sd : (long?)null;
                if (o.TryGetValue("bad", out var b) && b is List<object?> list)
                    foreach (var x in list)
                        if (x is string sha && IsSha256(sha) && !_bad.Contains(sha)) _bad.Add(sha);
            }
            catch (Exception)
            {
                // unreadable state: start over (the fallback or a fresh download)
            }
        }

        private static Entry? ReadEntry(Dictionary<string, object?> o, string key)
        {
            if (!o.TryGetValue(key, out var v) || !(v is Dictionary<string, object?> e)) return null;
            return e.TryGetValue("version", out var ver) && ver is string version && e.TryGetValue("sha256", out var h) && h is string sha
                   && IsSha256(sha)
                ? new Entry(version, sha)
                : null;
        }

        private void SaveState()
        {
            var b = new StringBuilder("{");
            void Field(string name, string json) => b.Append(b.Length > 1 ? "," : "").Append(JsonString(name)).Append(':').Append(json);
            string EntryJson(Entry e) => "{\"version\":" + JsonString(e.Version) + ",\"sha256\":" + JsonString(e.Sha256) + "}";
            if (_current != null) Field("current", EntryJson(_current));
            if (_previous != null) Field("previous", EntryJson(_previous));
            Field("failures", _failures.ToString(CultureInfo.InvariantCulture));
            Field("killed", _killed ? "true" : "false");
            if (_serial.HasValue) Field("serial", _serial.Value.ToString(CultureInfo.InvariantCulture));
            Field("bad", "[" + string.Join(",", _bad.ConvertAll(JsonString)) + "]");
            b.Append('}');
            try
            {
                WriteAtomically(_stateFile, Encoding.UTF8.GetBytes(b.ToString()));
            }
            catch (Exception e)
            {
                Report(null, E7Status.State, "cannot save the module state: " + Describe(e));
            }
        }

        private static void WriteAtomically(string path, byte[] data)
        {
            var dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            var tmp = path + ".tmp";
            using (var f = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None))
            {
                f.Write(data, 0, data.Length);
                f.Flush(true);
            }
            if (File.Exists(path)) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
        }

        // ------------------------------------------------------------------ hashing, signatures, manifest
        internal static byte[] Sha256(byte[] data)
        {
            using (var sha = SHA256.Create()) return sha.ComputeHash(data);
        }

        /// <summary>Lowercase hex SHA-256 of a module file, as in the manifest's sha256 and <see cref="ActiveModule.Sha256"/>.</summary>
        public static string Sha256Hex(byte[] data) => Hex(Sha256(data));

        internal static string Hex(byte[] b)
        {
            var s = new StringBuilder(b.Length * 2);
            foreach (var x in b) s.Append(x.ToString("x2", CultureInfo.InvariantCulture));
            return s.ToString();
        }

        private static bool IsSha256(string s)
        {
            if (s.Length != 64) return false;
            foreach (var c in s)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        }

        // "<manifest>.sig": 128 hex digits (e7c --sign-file), surrounding whitespace allowed; or 64 raw bytes
        internal static byte[]? ParseSignature(byte[]? raw)
        {
            if (raw == null) return null;
            if (raw.Length == 64) return (byte[])raw.Clone();
            var s = Encoding.ASCII.GetString(raw).Trim();
            if (s.Length != 128) return null;
            var o = new byte[64];
            for (int i = 0; i < 64; i++)
            {
                if (!byte.TryParse(s.Substring(2 * i, 2), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out o[i])) return null;
            }
            return o;
        }

        private static Failure Bad(string message) => new Failure(E7Status.Load, message);

        private static Manifest ParseManifest(byte[] body)
        {
            if (!(new Json(Encoding.UTF8.GetString(body)).ParseDocument() is Dictionary<string, object?> o)) throw Bad("not a JSON object");
            var m = new Manifest();
            o.TryGetValue("killSwitch", out var k);
            if (k != null && !(k is bool)) throw Bad("killSwitch must be a boolean");
            m.KillSwitch = k is bool kb && kb;
            o.TryGetValue("serial", out var s);
            if (s != null)
            {
                if (!(s is double d) || d != Math.Floor(d) || Math.Abs(d) >= 9.0e15) throw Bad("serial must be an integer");
                m.Serial = (long)d;
            }
            m.Version = o.TryGetValue("version", out var v) ? v as string : null;
            if (m.KillSwitch) return m;
            if (string.IsNullOrEmpty(m.Version)) throw Bad("missing version");
            if (!(o.TryGetValue("url", out var u) && u is string url)) throw Bad("missing url");
            m.Url = url;
            if (!(o.TryGetValue("sha256", out var h) && h is string hs && IsSha256(hs.ToLowerInvariant())))
                throw Bad("sha256 must be 64 hex digits");
            m.Sha256 = hs.ToLowerInvariant();
            if (!(o.TryGetValue("rolloutPercent", out var r) && r is double rp && rp >= 0 && rp <= 100))
                throw Bad("rolloutPercent must be a number from 0 to 100");
            m.RolloutPercent = rp;
            o.TryGetValue("minVm", out var mv);
            if (mv is string mvs) m.MinVm = ParseVersion(mvs) ?? throw Bad("invalid minVm " + mvs);
            else if (mv is double mvd && mvd >= 0 && mvd <= uint.MaxValue) m.MinVm = (uint)mvd;
            else if (mv != null) throw Bad("minVm must be a version string");
            return m;
        }

        // "0.2", "0.2.1", "v0.2" -> major << 16 | minor << 8 | patch
        internal static uint? ParseVersion(string s)
        {
            var t = s.StartsWith("v", StringComparison.Ordinal) ? s.Substring(1) : s;
            var parts = t.Split('.');
            if (parts.Length < 2 || parts.Length > 3) return null;
            uint v = 0;
            for (int i = 0; i < 3; i++)
            {
                uint x = 0;
                if (i < parts.Length)
                {
                    var p = parts[i];
                    if (p.Length < 1 || p.Length > 3) return null;
                    foreach (var c in p)
                        if (c < '0' || c > '9') return null;
                    x = uint.Parse(p, CultureInfo.InvariantCulture);
                    if (x > 255) return null;
                }
                v = v << 8 | x;
            }
            return v;
        }

        private static string JsonString(string s)
        {
            var b = new StringBuilder("\"");
            foreach (var c in s)
            {
                switch (c)
                {
                    case '"': b.Append("\\\""); break;
                    case '\\': b.Append("\\\\"); break;
                    case '\n': b.Append("\\n"); break;
                    case '\r': b.Append("\\r"); break;
                    case '\t': b.Append("\\t"); break;
                    default:
                        if (c < 0x20) b.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else b.Append(c);
                        break;
                }
            }
            return b.Append('"').ToString();
        }

        /// <summary>Minimal JSON reader (objects, arrays, strings, numbers as double, booleans, null).</summary>
        private sealed class Json
        {
            private readonly string _s;
            private int _i;

            public Json(string s) { _s = s; }

            public object? ParseDocument()
            {
                var v = Value(0);
                Ws();
                if (_i != _s.Length) throw Err("trailing characters");
                return v;
            }

            private Failure Err(string what) => Bad($"invalid JSON at {_i}: {what}");

            private void Ws()
            {
                while (_i < _s.Length && " \t\r\n".IndexOf(_s[_i]) >= 0) _i++;
            }

            private bool At(char c) => _i < _s.Length && _s[_i] == c;

            private bool Literal(string w)
            {
                if (string.CompareOrdinal(_s, _i, w, 0, w.Length) != 0) return false;
                _i += w.Length;
                return true;
            }

            private object? Value(int depth)
            {
                if (depth > 64) throw Err("nested too deeply");
                Ws();
                if (_i >= _s.Length) throw Err("unexpected end");
                char c = _s[_i];
                if (c == '{')
                {
                    _i++;
                    var o = new Dictionary<string, object?>();
                    Ws();
                    if (At('}')) { _i++; return o; }
                    while (true)
                    {
                        Ws();
                        if (!At('"')) throw Err("expected a key");
                        var k = ReadString();
                        Ws();
                        if (!At(':')) throw Err("expected ':'");
                        _i++;
                        o[k] = Value(depth + 1);
                        Ws();
                        if (At(',')) { _i++; continue; }
                        if (At('}')) { _i++; return o; }
                        throw Err("expected ',' or '}'");
                    }
                }
                if (c == '[')
                {
                    _i++;
                    var a = new List<object?>();
                    Ws();
                    if (At(']')) { _i++; return a; }
                    while (true)
                    {
                        a.Add(Value(depth + 1));
                        Ws();
                        if (At(',')) { _i++; continue; }
                        if (At(']')) { _i++; return a; }
                        throw Err("expected ',' or ']'");
                    }
                }
                if (c == '"') return ReadString();
                if (Literal("true")) return true;
                if (Literal("false")) return false;
                if (Literal("null")) return null;
                int start = _i;
                while (_i < _s.Length && "+-0123456789.eE".IndexOf(_s[_i]) >= 0) _i++;
                if (start == _i) throw Err("unexpected character");
                if (!double.TryParse(_s.Substring(start, _i - start), NumberStyles.Float, CultureInfo.InvariantCulture, out var d))
                    throw Err("bad number");
                return d;
            }

            private string ReadString()
            {
                _i++;  // opening quote
                var b = new StringBuilder();
                while (true)
                {
                    if (_i >= _s.Length) throw Err("unterminated string");
                    char c = _s[_i++];
                    if (c == '"') return b.ToString();
                    if (c != '\\') { b.Append(c); continue; }
                    if (_i >= _s.Length) throw Err("unterminated string");
                    char e = _s[_i++];
                    switch (e)
                    {
                        case '"': case '\\': case '/': b.Append(e); break;
                        case 'b': b.Append('\b'); break;
                        case 'f': b.Append('\f'); break;
                        case 'n': b.Append('\n'); break;
                        case 'r': b.Append('\r'); break;
                        case 't': b.Append('\t'); break;
                        case 'u':
                            if (_i + 4 > _s.Length
                                || !ushort.TryParse(_s.Substring(_i, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var u))
                                throw Err("bad escape");
                            b.Append((char)u);
                            _i += 4;
                            break;
                        default: throw Err("bad escape");
                    }
                }
            }
        }
    }
}
