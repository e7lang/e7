import java.nio.charset.StandardCharsets;
import org.e7vm.*;

public class E7VMTest {
  static int failures = 0;
  static void check(boolean c, String what) {
    if (!c) { System.err.println("FAILED: " + what); failures++; }
  }

  public static void main(String[] a) throws Exception {
    String src = String.join("\n",
        "import { hmac } from 'e7:crypto';",
        "import { hexEncode } from 'e7:encoding';",
        "import type { KeyHandle } from 'e7:crypto';",
        "declare function hostTime(): number;",
        "declare function hostTag(s: string, n: number): string;",
        "export function add(a: number, b: number): number { return a + b; }",
        "export function greet(n: string): string { console.log('greeting ' + n); return `héllo ${n} 😀`; }",
        "export function rev(b: Uint8Array): Uint8Array { return b.slice().reverse(); }",
        "export function big(x: u64): u64 { return x * 2; }",
        "export function sign(k: KeyHandle, s: string): string { return hexEncode(hmac('sha256', k, new TextEncoder().encode(s))); }",
        "export function host(): string { return hostTag('t', hostTime()); }",
        "export function boom(): number { throw new Error('kaboom'); }",
        "export function spin(n: number): number { let s = 0; for (let i = 0; i < n; i++) s += i; return s; }");
    byte[] code = E7VM.compile(src, "app.ts");
    byte[] seed = new byte[32];
    for (int i = 0; i < 32; i++) seed[i] = (byte) (i + 1);
    byte[] signed = E7VM.sign(code, seed);
    byte[] pub = E7VM.publicKey(seed);
    StringBuilder printed = new StringBuilder();
    try (E7VM vm = new E7VM(new E7VM.Config().requireSignature(pub).suspendOnBudget(true))) {
      vm.setPrintHandler(printed::append);
      vm.registerNative("hostTime", ">n", args -> 42.0);
      vm.registerNative("hostTag", "sn>s", args -> args[0] + "#" + ((Double) args[1]).intValue());
      vm.setKeyProvider(id -> id == 1 ? "secret".getBytes(StandardCharsets.UTF_8) : null);
      try {
        vm.load(code);
        check(false, "unsigned module must be rejected");
      } catch (E7Exception e) {
        check(e.status == E7Exception.SIGNATURE, "signature status");
      }
      E7VM.Module m = vm.load(signed);
      check(((Double) vm.call(m, "add", 2.0, 40)) == 42.0, "add");
      check("héllo Zoë 😀".equals(vm.call(m, "greet", "Zoë")), "greet unicode");
      check(printed.toString().equals("greeting Zoë"), "print handler");
      byte[] r = (byte[]) vm.call(m, "rev", (Object) new byte[] {1, 2, 3});
      check(r.length == 3 && r[0] == 3, "bytes");
      check(((Long) vm.call(m, "big", Long.MAX_VALUE)) == -2L, "u64 wrap");
      String mac = (String) vm.call(m, "sign", new KeyHandle(1), "payload");
      check(mac.length() == 64, "hmac via key handle");
      check("t#42".equals(vm.call(m, "host")), "host natives");
      try {
        vm.call(m, "boom");
        check(false, "exception expected");
      } catch (E7Exception e) {
        check(e.status == E7Exception.RUNTIME && e.getMessage().contains("kaboom"), "runtime error");
      }
      vm.setBudget(1000);
      int slices = 0;
      Object res;
      try {
        res = vm.call(m, "spin", 100000.0);
      } catch (E7Exception e) {
        check(e.status == E7Exception.SUSPENDED, "suspended");
        res = null;
        while (res == null) {
          slices++;
          vm.setBudget(1000);
          try { res = vm.resume(); } catch (E7Exception e2) { check(e2.status == E7Exception.SUSPENDED, "resume"); }
        }
      }
      check(((Double) res) == 4999950000.0 && slices > 10, "suspend/resume result");
      vm.setBudget(0);
      try {
        vm.call(m, "add", "x", 1);
        check(false, "args error expected");
      } catch (E7Exception e) {
        check(e.status == E7Exception.ARGS, "args status");
      }
    }
    // version number, inspect, GC statistics and growth setting, detached signatures
    int vn = E7VM.versionNumber();
    check(E7VM.version().equals((vn >> 16) + "." + ((vn >> 8) & 255) + "." + (vn & 255)) && vn >= 0x000100, "versionNumber");
    E7VM.ModuleInfo info = E7VM.inspect(signed);
    check(info.canRun() && info.format == 3 && info.requiresVersion == 0x000100 && info.isSigned && !info.hasDebugInfo,
        "inspect signed module");
    check("0.1.0".equals(info.requiresVersionString()) && info.keyId.length == 8 && (info.keyId[0] | info.keyId[7]) != 0, "inspect key id");
    check(!E7VM.inspect(code).isSigned, "inspect unsigned module");
    check(E7VM.inspect(new byte[] {1, 2, 3}).status == E7Exception.LOAD, "inspect garbage");
    try (E7VM vm = new E7VM(new E7VM.Config().gcGrowthPercent(150))) {
      E7VM.Module m = vm.loadSource("let keep: number[][] = [];\n"
          + "export function churn(): number { for (let i = 0; i < 20000; i++) { keep.push([i]); if (keep.length > 500) keep = []; } return keep.length; }",
          "gc.ts");
      vm.call(m, "churn");
      vm.collectGarbage();
      E7VM.GcStats gs = vm.gcStats();
      check(gs.collections >= 1 && gs.maxPauseNanos > 0 && gs.totalPauseNanos >= gs.maxPauseNanos, "gc stats: " + gs);
      check(gs.heapBytes == vm.memoryUsed() && gs.peakHeapBytes >= gs.heapBytes, "gc stats heap");
    }
    byte[] manifest = "{\"version\":\"2\",\"killSwitch\":false}".getBytes(StandardCharsets.UTF_8);
    byte[] dsig = E7VM.signDetached(manifest, seed);
    StringBuilder hex = new StringBuilder();
    for (byte b : dsig) hex.append(String.format("%02x", b & 0xff));
    check(hex.toString().equals("1549a30db54e6fc64e15efee8a9b5222a22be751830dd98dd5f97d30c1bc6b09"
        + "2cdaa2efdc5a609a338b827ca32a0a9d1d9212ffd3eb51bfc9eb3b073487e607"), "signDetached known answer");
    byte[] otherPub = E7VM.publicKey(new byte[32]);
    check(E7VM.verifyDetached(manifest, dsig, pub) && E7VM.verifyDetached(manifest, dsig, otherPub, pub), "verifyDetached");
    check(!E7VM.verifyDetached(manifest, dsig, otherPub) && !E7VM.verifyDetached("x".getBytes(), dsig, pub), "verifyDetached rejects");

    if (failures > 0) { System.err.println(failures + " failure(s)"); System.exit(1); }
    System.out.println("E7VMTest: all checks passed");
  }
}
