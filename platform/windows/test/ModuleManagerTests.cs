// E7ModuleManager scenarios (same as platform/android/shared-test/org/e7vm/ModuleManagerScenarios.java and
// Tests/E7VMTests/E7ModuleManagerTests.swift), run by Program.Main.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using E7;

static class ModuleManagerTests
{
    const string Base = "https://updates.example.com/app/";
    const string ManifestUrl = Base + "manifest.json";
    static readonly byte[] SeedKey = Seed(1), RotatedSeed = Seed(0x41), UntrustedSeed = Seed(0x81);

    sealed class CheckFailed : Exception
    {
        public CheckFailed(string what) : base(what) { }
    }

    static void Check(bool cond, string what)
    {
        if (!cond) throw new CheckFailed(what);
    }

    /// <summary>Runs every scenario; returns the number that failed.</summary>
    public static int Run()
    {
        var scenarios = new (string Name, Action Body)[]
        {
            ("happyPath", HappyPath), ("badSha256", BadSha256), ("badManifestSignature", BadManifestSignature),
            ("killSwitch", KillSwitch), ("rolloutExclusion", RolloutExclusion), ("requiresNewerVm", RequiresNewerVm),
            ("selfTestFailure", SelfTestFailure), ("rollbackAfterFailures", RollbackAfterFailures), ("asyncExports", AsyncExports),
        };
        int failures = 0;
        foreach (var (name, body) in scenarios)
        {
            try
            {
                body();
                Console.WriteLine($"ok   module manager: {name}");
            }
            catch (Exception e)
            {
                failures++;
                Console.WriteLine($"FAIL module manager: {name}: {e}");
            }
        }
        return failures;
    }

    // ------------------------------------------------------------------ scenarios
    static void HappyPath()
    {
        using var f = new Fixture();
        var v2 = Module(2, true);
        f.Server.Publish("2", v2, "100", 1, SeedKey);
        var mm = f.Manager();
        var a = mm.Start();
        Check(a.IsFallback && Answer(a) == 0 && a.Version == "bundled", "start without a stored module: fallback");
        a = mm.Update();
        Check(!a.IsFallback && a.Version == "2" && Answer(a) == 2, "update activates version 2");
        Check(E7ModuleManager.Sha256Hex(v2) == a.Sha256 && a == mm.Active, "active module and sha256");
        Check(f.Reports.Lines.Count == 0, "no errors: " + f.Reports);
        f.Server.Fetched.Clear();
        Check(mm.Update() == a && !f.Server.Fetched.Contains(Base + "app-2.e7b"), "up to date: no second download");
        // restart without network: the last-known-good module comes from disk
        f.Server.Offline = true;
        var mm2 = f.Manager();
        a = mm2.Start();
        Check(!a.IsFallback && a.Version == "2" && Answer(a) == 2, "restart offline: stored module");
        a = mm2.Update();
        Check(a.Version == "2" && f.Reports.Has(null, E7Status.Load, "manifest: offline"), "offline update keeps it: " + f.Reports);
        // key rotation: a manifest signed with the second trusted key
        f.Server.Offline = false;
        f.Server.Publish("3", Module(3, true), "100", 2, RotatedSeed);
        a = mm2.Update();
        Check(a.Version == "3" && Answer(a) == 3, "manifest signed by the rotated key");
        // a different user id must give the same result at 100 %
        f.UserId = "someone-else";
        Check(Answer(f.Manager().Update()) == 3, "rollout 100 % includes everybody");
    }

    static void BadSha256()
    {
        using var f = new Fixture();
        f.Server.Publish("2", Module(2, true), "100", 1, SeedKey);
        f.Server.Files[Base + "app-2.e7b"] = Module(5, true);  // the CDN serves other bytes
        var a = f.Manager().Update();
        Check(a.IsFallback && Answer(a) == 0, "sha256 mismatch: fallback");
        Check(f.Reports.Has("2", E7Status.Signature, "sha256 mismatch"), "sha256 mismatch reported: " + f.Reports);
        var modules = Path.Combine(f.Dir, "modules");
        Check(!Directory.Exists(modules) || Directory.GetFiles(modules).Length == 0, "nothing stored");
    }

    static void BadManifestSignature()
    {
        using var f = new Fixture();
        f.Server.Publish("2", Module(2, true), "100", 1, UntrustedSeed);
        var mm = f.Manager();
        Check(mm.Update().IsFallback, "manifest signed by an untrusted key: ignored");
        Check(f.Reports.Has(null, E7Status.Signature, "manifest signature"), "reported: " + f.Reports);
        Check(!f.Server.Fetched.Contains(Base + "app-2.e7b"), "module not downloaded");
        f.Server.Publish("2", Module(2, true), "100", 1, SeedKey);
        Check(Answer(mm.Update()) == 2, "valid manifest activates version 2");
        // a forged kill switch: tampered body with the old signature, or signed by an untrusted key
        f.Reports.Clear();
        f.Server.Files[ManifestUrl] = Encoding.UTF8.GetBytes("{\"killSwitch\":true,\"serial\":9}");
        Check(Answer(mm.Update()) == 2, "tampered kill switch ignored");
        f.Server.Manifest("{\"killSwitch\":true,\"serial\":9}", UntrustedSeed);
        Check(Answer(mm.Update()) == 2, "kill switch signed by an untrusted key ignored");
        f.Server.Files[ManifestUrl + ".sig"] = Encoding.UTF8.GetBytes("not a signature");
        Check(Answer(mm.Update()) == 2, "malformed signature file ignored");
        Check(f.Reports.Count(E7Status.Signature) == 3, "three signature errors: " + f.Reports);
        Check(!f.Manager().Start().IsFallback, "the kill switch was not persisted");
    }

    static void KillSwitch()
    {
        using var f = new Fixture();
        f.Server.Publish("2", Module(2, true), "100", 1, SeedKey);
        var v2Manifest = Encoding.UTF8.GetString(f.Server.Files[ManifestUrl]);
        var mm = f.Manager();
        Check(Answer(mm.Update()) == 2, "version 2 active");
        f.Server.Manifest("{\"killSwitch\":true,\"serial\":2}", SeedKey);
        var a = mm.Update();
        Check(a.IsFallback && Answer(a) == 0, "kill switch: fallback");
        f.Server.Offline = true;
        Check(f.Manager().Start().IsFallback, "kill switch persists across restarts");
        f.Server.Offline = false;
        // replaying the older manifest (serial 1) must not undo the kill switch
        f.Server.Manifest(v2Manifest, SeedKey);
        Check(mm.Update().IsFallback && f.Reports.Has("2", E7Status.Signature, "stale manifest"), "replay rejected: " + f.Reports);
        f.Server.Manifest(v2Manifest.Replace(",\"serial\":1", ""), SeedKey);
        Check(mm.Update().IsFallback, "manifest without serial rejected after a serial was seen");
        // lifting the kill switch reuses the stored module
        f.Server.Fetched.Clear();
        f.Server.Manifest(v2Manifest.Replace("\"serial\":1", "\"serial\":3"), SeedKey);
        a = mm.Update();
        Check(!a.IsFallback && Answer(a) == 2 && !f.Server.Fetched.Contains(Base + "app-2.e7b"), "kill switch lifted");
    }

    static void RolloutExclusion()
    {
        // documented bucket: SHA-256("<version>:<userId>"), first 4 bytes big-endian, mod 10000
        Check(E7ModuleManager.RolloutBucket("user-42", "2") == 2213, "bucket user-42 / 2");
        Check(E7ModuleManager.RolloutBucket("user-7", "2") == 4586, "bucket user-7 / 2");
        Check(E7ModuleManager.RolloutBucket("alice", "3") == 6058, "bucket alice / 3");
        using (var f = new Fixture())  // userId user-42: bucket 2213
        {
            var v2 = Module(2, true);
            f.Server.Publish("2", v2, "0", 1, SeedKey);
            var mm = f.Manager();
            Check(mm.Update().IsFallback && !f.Server.Fetched.Contains(Base + "app-2.e7b"), "0 %: excluded, nothing downloaded");
            f.Server.Publish("2", v2, "22", 2, SeedKey);
            Check(mm.Update().IsFallback, "22 %: bucket 2213 excluded");
            f.Server.Publish("2", v2, "22.14", 3, SeedKey);
            Check(Answer(mm.Update()) == 2, "22.14 %: bucket 2213 included");
            Check(f.Reports.Lines.Count == 0, "exclusion is not an error: " + f.Reports);
            // rollout of version 3 that excludes this user: the stored version 2 stays
            f.Server.Publish("3", Module(3, true), "1", 4, SeedKey);
            Check(Answer(mm.Update()) == 2, "excluded from version 3: keeps version 2");
        }
        using (var f = new Fixture { UserId = "user-7" })  // bucket 4586
        {
            f.Server.Publish("2", Module(2, true), "45.86", 1, SeedKey);
            Check(f.Manager().Update().IsFallback, "user-7 excluded at 45.86 %");
            f.Server.Publish("2", Module(2, true), "45.87", 2, SeedKey);
            Check(Answer(f.Manager().Update()) == 2, "user-7 included at 45.87 %");
        }
    }

    static void RequiresNewerVm()
    {
        using var f = new Fixture();
        f.Server.Publish("2", Module(2, true), "100", 1, SeedKey, ",\"minVm\":\"99.0\"");
        var mm = f.Manager();
        Check(mm.Update().IsFallback, "minVm 99.0: skipped");
        Check(f.Reports.Has("2", E7Status.Version, "requires e7vm 99.0.0"), "reported: " + f.Reports);
        Check(!f.Server.Fetched.Contains(Base + "app-2.e7b"), "not downloaded");
        // the module header itself requires a newer VM (manifest without minVm)
        var newer = E7VM.Sign(WithRequires(E7VM.Compile(Source(3, true), "app.ts"), 0x630000), SeedKey);
        var info = E7VM.Inspect(newer);
        Check(info.Status == E7Status.Version && info.RequiresVersionString == "99.0.0" && info.IsSigned, "inspect: requires 99.0.0");
        f.Server.Publish("3", newer, "100", 2, SeedKey);
        Check(mm.Update().IsFallback, "module requiring e7vm 99: skipped");
        Check(f.Reports.Has("3", E7Status.Version, "needs e7vm 99.0.0"), "reported: " + f.Reports);
        // a VM-compatible version is still taken
        f.Server.Publish("4", Module(4, true), "100", 3, SeedKey, ",\"minVm\":\"" + E7VM.Version + "\"");
        Check(Answer(mm.Update()) == 4, "minVm = this VM: activated");
    }

    static void SelfTestFailure()
    {
        using var f = new Fixture();
        f.Server.Publish("2", Module(2, false), "100", 1, SeedKey);
        var mm = f.Manager();
        Check(mm.Update().IsFallback, "selfTest false: not activated");
        Check(f.Reports.Has("2", E7Status.Runtime, "selfTest() returned false"), "reported: " + f.Reports);
        f.Server.Publish("3", Module(3, true), "100", 2, SeedKey);
        Check(Answer(mm.Update()) == 3, "version 3 activated");
        f.Server.Publish("4", Module(4, false), "100", 3, SeedKey);
        Check(Answer(mm.Update()) == 3, "failing selfTest of version 4: version 3 stays");
        f.Server.Offline = true;
        Check(Answer(f.Manager().Start()) == 3, "restart: version 3");
    }

    static void RollbackAfterFailures()
    {
        using var f = new Fixture();
        var mm = f.Manager();
        f.Server.Publish("2", Module(2, true), "100", 1, SeedKey);
        Check(Answer(mm.Update()) == 2, "version 2");
        f.Server.Publish("3", Module(3, true), "100", 2, SeedKey);
        Check(Answer(mm.Update()) == 3, "version 3");
        mm.ReportFailure(E7Status.Runtime, "boom");
        mm.ReportFailure(E7Status.Runtime, "boom");
        mm.ReportSuccess();  // resets the count
        mm.ReportFailure(E7Status.Runtime, "boom");
        mm.ReportFailure(E7Status.Runtime, "boom");
        Check(Answer(mm.Active!) == 3 && f.Reports.Lines.Count == 0, "2 consecutive failures: still version 3");
        mm.ReportFailure(E7Status.Budget, "too slow");
        Check(Answer(mm.Active!) == 2 && mm.Active!.Version == "2", "3rd consecutive failure: rolled back to 2");
        Check(f.Reports.Has("3", E7Status.Budget, "rolled back after 3 consecutive failures: too slow"), "rollback reported: " + f.Reports);
        f.Server.Fetched.Clear();
        Check(Answer(mm.Update()) == 2 && !f.Server.Fetched.Contains(Base + "app-3.e7b"), "rolled-back version not taken again");
        f.Server.Offline = true;
        var mm2 = f.Manager();
        Check(Answer(mm2.Start()) == 2, "restart: version 2");
        for (int i = 0; i < 3; i++) mm2.ReportFailure(E7Status.Runtime, "bad");
        Check(mm2.Active!.IsFallback, "version 2 fails too: fallback");
        Check(f.Manager().Start().IsFallback, "restart after both rollbacks: fallback");
        f.Server.Offline = false;
        f.Server.Publish("4", Module(4, true), "100", 3, SeedKey);
        Check(Answer(mm2.Update()) == 4, "a new version recovers");
    }

    // a module whose top level and selfTest await an async host function (completed on another thread)
    static void AsyncExports()
    {
        using var f = new Fixture();
        f.SetUp = vm => vm.RegisterAsyncNative("hostValue", ">n", async args =>
        {
            await Task.Delay(1);
            return 7.0;
        });
        const string src = "declare function hostValue(): Promise<number>;\n"
            + "const base = await hostValue();\n"
            + "export async function selfTest(): Promise<boolean> { return (await hostValue()) === 7; }\n"
            + "export async function quote(x: number): Promise<number> { return base + x + (await hostValue()); }\n"
            + "export function answer(): number { return 9; }\n";
        f.Server.Publish("9", E7VM.Sign(E7VM.Compile(src, "app.ts"), SeedKey), "100", 1, SeedKey);
        var mm = f.Manager();
        mm.Start();
        var a = mm.Update();
        Check(!a.IsFallback && Answer(a) == 9 && f.Reports.Lines.Count == 0, "async top level and selfTest: activated " + f.Reports);
        Check(a.CallAwait("quote", 1.0) is double q1 && q1 == 15, "ActiveModule.CallAwait");
        Check(a.CallAsync("quote", 2.0).GetAwaiter().GetResult() is double q2 && q2 == 16, "ActiveModule.CallAsync");
        try
        {
            a.Call("quote", 1.0);
            Check(false, "a synchronous call of a pending export must fail");
        }
        catch (E7Exception e)
        {
            Check(e.IsPending, "ActiveModule.Call: Pending");
        }
        Check(Answer(f.Manager().Start()) == 9, "restart: the stored module's top level awaits again");
    }

    // ------------------------------------------------------------------ helpers
    static byte[] Seed(int start) => Enumerable.Range(0, 32).Select(i => (byte)(start + i)).ToArray();

    static string Source(int answer, bool selfTestOk) =>
        $"export function selfTest(): boolean {{ return answer() === {(selfTestOk ? answer : -1)}; }}\n"
        + $"export function answer(): number {{ return {answer}; }}\n";

    static byte[] Module(int answer, bool selfTestOk) => E7VM.Sign(E7VM.Compile(Source(answer, selfTestOk), "app.ts"), SeedKey);

    static double Answer(E7ModuleManager.ActiveModule a) => (double)a.Call("answer")!;

    // patches the `requires` header field (offset 12) and the checksum (CRC-32 of all but the last 8 bytes)
    static byte[] WithRequires(byte[] code, uint version)
    {
        var b = (byte[])code.Clone();
        BitConverter.GetBytes(version).CopyTo(b, 12);
        uint c = 0xFFFFFFFFu;
        for (int i = 0; i < b.Length - 8; i++)
        {
            c ^= b[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        BitConverter.GetBytes(~c).CopyTo(b, b.Length - 4);
        return b;
    }

    /// <summary>In-memory update server.</summary>
    sealed class Server
    {
        public readonly Dictionary<string, byte[]> Files = new Dictionary<string, byte[]>();
        public readonly List<string> Fetched = new List<string>();
        public bool Offline;

        public byte[] Fetch(Uri url)
        {
            var u = url.AbsoluteUri;
            Fetched.Add(u);
            if (Offline) throw new IOException("offline");
            return Files.TryGetValue(u, out var b) ? b : throw new IOException("404 " + u);
        }

        public void Publish(string version, byte[] module, string rollout, long serial, byte[] seed, string extra = "")
        {
            Files[Base + "app-" + version + ".e7b"] = module;
            Manifest("{\"version\":\"" + version + "\",\"url\":\"app-" + version + ".e7b\",\"sha256\":\""
                     + E7ModuleManager.Sha256Hex(module) + "\",\"rolloutPercent\":" + rollout + ",\"killSwitch\":false,\"serial\":"
                     + serial + extra + "}", seed);
        }

        public void Manifest(string json, byte[] seed)
        {
            var body = Encoding.UTF8.GetBytes(json);
            Files[ManifestUrl] = body;
            var sig = E7VM.SignDetached(body, seed);
            Files[ManifestUrl + ".sig"] = Encoding.ASCII.GetBytes(string.Concat(sig.Select(x => x.ToString("x2"))) + "\n");
        }
    }

    sealed class Reports
    {
        public readonly List<string> Lines = new List<string>();
        readonly List<E7Status> _statuses = new List<E7Status>();

        public void Add(string? version, E7Status status, string message)
        {
            Lines.Add($"{version ?? "null"} | {(int)status} | {message}");
            _statuses.Add(status);
        }

        public void Clear()
        {
            Lines.Clear();
            _statuses.Clear();
        }

        public bool Has(string? version, E7Status status, string fragment) =>
            Lines.Any(l => l.StartsWith($"{version ?? "null"} | {(int)status} | ", StringComparison.Ordinal) && l.Contains(fragment));

        public int Count(E7Status status) => _statuses.Count(s => s == status);

        public override string ToString() => "[" + string.Join("; ", Lines) + "]";
    }

    sealed class Fixture : IDisposable
    {
        public readonly string Dir = Path.Combine(Path.GetTempPath(), "e7mm-" + Guid.NewGuid().ToString("N"));
        public readonly Server Server = new Server();
        public readonly Reports Reports = new Reports();
        readonly List<E7ModuleManager> _managers = new List<E7ModuleManager>();
        public string UserId = "user-42";
        public Action<E7VM>? SetUp;

        public Fixture() { Directory.CreateDirectory(Dir); }

        public E7ModuleManager Manager()
        {
            var server = Server;
            var reports = Reports;
            var setUp = SetUp;
            var m = new E7ModuleManager(new E7ModuleManager.Options
            {
                ManifestUrl = new Uri(ManifestUrl),
                TrustedKeys = { E7VM.PublicKey(SeedKey), E7VM.PublicKey(RotatedSeed) },
                Fetcher = server.Fetch,
                StorageDirectory = Dir,
                UserId = UserId,
                FallbackModule = E7VM.Compile("export function answer(): number { return 0; }", "fallback.ts"),
                FallbackVersion = "bundled",
                MemoryLimit = 16 << 20,
                GcGrowthPercent = 150,
                SetUpVM = vm =>
                {
                    vm.SetBudget(10_000_000);
                    setUp?.Invoke(vm);
                },
                ReportError = reports.Add,
                MaxConsecutiveFailures = 3,
            });
            _managers.Add(m);
            return m;
        }

        public void Dispose()
        {
            foreach (var m in _managers) m.Dispose();
            try { Directory.Delete(Dir, true); } catch (IOException) { }
        }
    }
}
