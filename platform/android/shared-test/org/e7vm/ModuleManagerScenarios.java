package org.e7vm;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * E7ModuleManager scenarios without a test framework (each throws AssertionError on failure). Run by the
 * JUnit test E7ModuleManagerTest (Gradle :jvm:test and the Android instrumented tests) and by
 * platform/java/test/ModuleManagerTest.java (plain javac + java, CI job "jni").
 */
public final class ModuleManagerScenarios {
  static final String BASE = "https://updates.example.com/app/";
  static final String MANIFEST_URL = BASE + "manifest.json";
  static final byte[] SEED = seed(1), ROTATED_SEED = seed(0x41), UNTRUSTED_SEED = seed(0x81);

  public static final String[] ALL = {
    "happyPath", "badSha256", "badManifestSignature", "killSwitch", "rolloutExclusion", "requiresNewerVm",
    "selfTestFailure", "rollbackAfterFailures",
  };

  public static void run(String name) throws Exception {
    switch (name) {
      case "happyPath": happyPath(); break;
      case "badSha256": badSha256(); break;
      case "badManifestSignature": badManifestSignature(); break;
      case "killSwitch": killSwitch(); break;
      case "rolloutExclusion": rolloutExclusion(); break;
      case "requiresNewerVm": requiresNewerVm(); break;
      case "selfTestFailure": selfTestFailure(); break;
      case "rollbackAfterFailures": rollbackAfterFailures(); break;
      default: throw new IllegalArgumentException(name);
    }
  }

  // ------------------------------------------------------------------ scenarios
  static void happyPath() throws Exception {
    try (Fixture f = new Fixture()) {
      byte[] v2 = module(2, true);
      f.server.publish("2", v2, "100", 1, SEED);
      E7ModuleManager mm = f.manager();
      E7ModuleManager.Active a = mm.start();
      check(a.isFallback && answer(a) == 0 && "bundled".equals(a.version), "start without a stored module: fallback");
      a = mm.update();
      check(!a.isFallback && "2".equals(a.version) && answer(a) == 2, "update activates version 2");
      check(E7ModuleManager.sha256Hex(v2).equals(a.sha256) && a == mm.active(), "active module and sha256");
      check(f.reports.lines.isEmpty(), "no errors: " + f.reports.lines);
      f.server.fetched.clear();
      check(mm.update() == a && !f.server.fetched.contains(BASE + "app-2.e7b"), "up to date: no second download");
      // restart without network: the last-known-good module comes from disk
      f.server.offline = true;
      E7ModuleManager mm2 = f.manager();
      a = mm2.start();
      check(!a.isFallback && "2".equals(a.version) && answer(a) == 2, "restart offline: stored module");
      a = mm2.update();
      check("2".equals(a.version) && f.reports.has(null, E7Exception.LOAD, "manifest: offline"), "offline update keeps it");
      // key rotation: a manifest signed with the second trusted key
      f.server.offline = false;
      f.server.publish("3", module(3, true), "100", 2, ROTATED_SEED);
      a = mm2.update();
      check("3".equals(a.version) && answer(a) == 3, "manifest signed by the rotated key");
      // a different user id must give the same result at 100 %
      f.userId = "someone-else";
      check(answer(f.manager().update()) == 3, "rollout 100 % includes everybody");
    }
  }

  static void badSha256() throws Exception {
    try (Fixture f = new Fixture()) {
      f.server.publish("2", module(2, true), "100", 1, SEED);
      f.server.files.put(BASE + "app-2.e7b", module(5, true));  // the CDN serves other bytes
      E7ModuleManager mm = f.manager();
      E7ModuleManager.Active a = mm.update();
      check(a.isFallback && answer(a) == 0, "sha256 mismatch: fallback");
      check(f.reports.has("2", E7Exception.SIGNATURE, "sha256 mismatch"), "sha256 mismatch reported: " + f.reports.lines);
      String[] stored = new File(f.dir, "modules").list();
      check(stored == null || stored.length == 0, "nothing stored");
    }
  }

  static void badManifestSignature() throws Exception {
    try (Fixture f = new Fixture()) {
      f.server.publish("2", module(2, true), "100", 1, UNTRUSTED_SEED);
      E7ModuleManager mm = f.manager();
      check(mm.update().isFallback, "manifest signed by an untrusted key: ignored");
      check(f.reports.has(null, E7Exception.SIGNATURE, "manifest signature"), "reported: " + f.reports.lines);
      check(!f.server.fetched.contains(BASE + "app-2.e7b"), "module not downloaded");
      f.server.publish("2", module(2, true), "100", 1, SEED);
      check(answer(mm.update()) == 2, "valid manifest activates version 2");
      // a forged kill switch: tampered body with the old signature, or signed by an untrusted key
      f.reports.clear();
      f.server.files.put(MANIFEST_URL, "{\"killSwitch\":true,\"serial\":9}".getBytes(StandardCharsets.UTF_8));
      check(answer(mm.update()) == 2, "tampered kill switch ignored");
      f.server.manifest("{\"killSwitch\":true,\"serial\":9}", UNTRUSTED_SEED);
      check(answer(mm.update()) == 2, "kill switch signed by an untrusted key ignored");
      f.server.files.put(MANIFEST_URL + ".sig", "not a signature".getBytes(StandardCharsets.UTF_8));
      check(answer(mm.update()) == 2, "malformed signature file ignored");
      check(f.reports.count(E7Exception.SIGNATURE) == 3, "three signature errors: " + f.reports.lines);
      check(!f.manager().start().isFallback, "the kill switch was not persisted");
    }
  }

  static void killSwitch() throws Exception {
    try (Fixture f = new Fixture()) {
      f.server.publish("2", module(2, true), "100", 1, SEED);
      String v2Manifest = new String(f.server.files.get(MANIFEST_URL), StandardCharsets.UTF_8);
      E7ModuleManager mm = f.manager();
      check(answer(mm.update()) == 2, "version 2 active");
      f.server.manifest("{\"killSwitch\":true,\"serial\":2}", SEED);
      E7ModuleManager.Active a = mm.update();
      check(a.isFallback && answer(a) == 0, "kill switch: fallback");
      f.server.offline = true;
      check(f.manager().start().isFallback, "kill switch persists across restarts");
      f.server.offline = false;
      // replaying the older manifest (serial 1) must not undo the kill switch
      f.server.manifest(v2Manifest, SEED);
      check(mm.update().isFallback && f.reports.has("2", E7Exception.SIGNATURE, "stale manifest"), "replay rejected");
      f.server.manifest(v2Manifest.replace(",\"serial\":1", ""), SEED);
      check(mm.update().isFallback, "manifest without serial rejected after a serial was seen");
      // lifting the kill switch reuses the stored module
      f.server.fetched.clear();
      f.server.manifest(v2Manifest.replace("\"serial\":1", "\"serial\":3"), SEED);
      a = mm.update();
      check(!a.isFallback && answer(a) == 2 && !f.server.fetched.contains(BASE + "app-2.e7b"), "kill switch lifted");
    }
  }

  static void rolloutExclusion() throws Exception {
    // documented bucket: SHA-256("<version>:<userId>"), first 4 bytes big-endian, mod 10000
    check(E7ModuleManager.rolloutBucket("user-42", "2") == 2213, "bucket user-42 / 2");
    check(E7ModuleManager.rolloutBucket("user-7", "2") == 4586, "bucket user-7 / 2");
    check(E7ModuleManager.rolloutBucket("alice", "3") == 6058, "bucket alice / 3");
    try (Fixture f = new Fixture()) {  // userId user-42: bucket 2213
      byte[] v2 = module(2, true);
      f.server.publish("2", v2, "0", 1, SEED);
      E7ModuleManager mm = f.manager();
      check(mm.update().isFallback && !f.server.fetched.contains(BASE + "app-2.e7b"), "0 %: excluded, nothing downloaded");
      f.server.publish("2", v2, "22", 2, SEED);
      check(mm.update().isFallback, "22 %: bucket 2213 excluded");
      f.server.publish("2", v2, "22.14", 3, SEED);
      check(answer(mm.update()) == 2, "22.14 %: bucket 2213 included");
      check(f.reports.lines.isEmpty(), "exclusion is not an error: " + f.reports.lines);
      // rollout of version 3 that excludes this user: the stored version 2 stays
      f.server.publish("3", module(3, true), "1", 4, SEED);
      check(answer(mm.update()) == 2, "excluded from version 3: keeps version 2");
    }
    try (Fixture f = new Fixture()) {
      f.userId = "user-7";  // bucket 4586
      f.server.publish("2", module(2, true), "45.86", 1, SEED);
      check(f.manager().update().isFallback, "user-7 excluded at 45.86 %");
      f.server.publish("2", module(2, true), "45.87", 2, SEED);
      check(answer(f.manager().update()) == 2, "user-7 included at 45.87 %");
    }
  }

  static void requiresNewerVm() throws Exception {
    try (Fixture f = new Fixture()) {
      f.server.publish("2", module(2, true), "100", 1, SEED, ",\"minVm\":\"99.0\"");
      E7ModuleManager mm = f.manager();
      check(mm.update().isFallback, "minVm 99.0: skipped");
      check(f.reports.has("2", E7Exception.VERSION, "requires e7vm 99.0.0"), "reported: " + f.reports.lines);
      check(!f.server.fetched.contains(BASE + "app-2.e7b"), "not downloaded");
      // the module header itself requires a newer VM (manifest without minVm)
      byte[] newer = E7VM.sign(withRequires(E7VM.compile(source(3, true), "app.ts"), 0x630000), SEED);
      E7VM.ModuleInfo info = E7VM.inspect(newer);
      check(info.status == E7Exception.VERSION && "99.0.0".equals(info.requiresVersionString()) && info.isSigned,
          "inspect: requires 99.0.0");
      f.server.publish("3", newer, "100", 2, SEED);
      check(mm.update().isFallback, "module requiring e7vm 99: skipped");
      check(f.reports.has("3", E7Exception.VERSION, "needs e7vm 99.0.0"), "reported: " + f.reports.lines);
      // a VM-compatible version is still taken
      f.server.publish("4", module(4, true), "100", 3, SEED, ",\"minVm\":\"" + E7VM.version() + "\"");
      check(answer(mm.update()) == 4, "minVm = this VM: activated");
    }
  }

  static void selfTestFailure() throws Exception {
    try (Fixture f = new Fixture()) {
      f.server.publish("2", module(2, false), "100", 1, SEED);
      E7ModuleManager mm = f.manager();
      check(mm.update().isFallback, "selfTest false: not activated");
      check(f.reports.has("2", E7Exception.RUNTIME, "selfTest() returned false"), "reported: " + f.reports.lines);
      f.server.publish("3", module(3, true), "100", 2, SEED);
      check(answer(mm.update()) == 3, "version 3 activated");
      f.server.publish("4", module(4, false), "100", 3, SEED);
      check(answer(mm.update()) == 3, "failing selfTest of version 4: version 3 stays");
      f.server.offline = true;
      check(answer(f.manager().start()) == 3, "restart: version 3");
    }
  }

  static void rollbackAfterFailures() throws Exception {
    try (Fixture f = new Fixture()) {
      E7ModuleManager mm = f.manager();
      f.server.publish("2", module(2, true), "100", 1, SEED);
      check(answer(mm.update()) == 2, "version 2");
      f.server.publish("3", module(3, true), "100", 2, SEED);
      check(answer(mm.update()) == 3, "version 3");
      mm.reportFailure(E7Exception.RUNTIME, "boom");
      mm.reportFailure(E7Exception.RUNTIME, "boom");
      mm.reportSuccess();  // resets the count
      mm.reportFailure(E7Exception.RUNTIME, "boom");
      mm.reportFailure(E7Exception.RUNTIME, "boom");
      check(answer(mm.active()) == 3 && f.reports.lines.isEmpty(), "2 consecutive failures: still version 3");
      mm.reportFailure(E7Exception.BUDGET, "too slow");
      check(answer(mm.active()) == 2 && "2".equals(mm.active().version), "3rd consecutive failure: rolled back to 2");
      check(f.reports.has("3", E7Exception.BUDGET, "rolled back after 3 consecutive failures: too slow"), "rollback reported: " + f.reports.lines);
      f.server.fetched.clear();
      check(answer(mm.update()) == 2 && !f.server.fetched.contains(BASE + "app-3.e7b"), "rolled-back version not taken again");
      f.server.offline = true;
      E7ModuleManager mm2 = f.manager();
      check(answer(mm2.start()) == 2, "restart: version 2");
      for (int i = 0; i < 3; i++) mm2.reportFailure(E7Exception.RUNTIME, "bad");
      check(mm2.active().isFallback, "version 2 fails too: fallback");
      check(f.manager().start().isFallback, "restart after both rollbacks: fallback");
      f.server.offline = false;
      f.server.publish("4", module(4, true), "100", 3, SEED);
      check(answer(mm2.update()) == 4, "a new version recovers");
    }
  }

  // ------------------------------------------------------------------ helpers
  static void check(boolean cond, String what) {
    if (!cond) throw new AssertionError(what);
  }

  static byte[] seed(int start) {
    byte[] s = new byte[32];
    for (int i = 0; i < 32; i++) s[i] = (byte) (start + i);
    return s;
  }

  static String source(int answer, boolean selfTestOk) {
    return "export function selfTest(): boolean { return answer() === " + (selfTestOk ? answer : -1) + "; }\n"
        + "export function answer(): number { return " + answer + "; }\n";
  }

  static byte[] module(int answer, boolean selfTestOk) {
    return E7VM.sign(E7VM.compile(source(answer, selfTestOk), "app.ts"), SEED);
  }

  static double answer(E7ModuleManager.Active a) { return (Double) a.call("answer"); }

  // patches the `requires` header field (offset 12) and the checksum (CRC-32 of all but the last 8 bytes)
  static byte[] withRequires(byte[] code, int version) {
    byte[] b = code.clone();
    for (int i = 0; i < 4; i++) b[12 + i] = (byte) (version >>> (8 * i));
    CRC32 crc = new CRC32();
    crc.update(b, 0, b.length - 8);
    long c = crc.getValue();
    for (int i = 0; i < 4; i++) b[b.length - 4 + i] = (byte) (c >>> (8 * i));
    return b;
  }

  /** In-memory update server. */
  static final class Server implements E7ModuleManager.Fetcher {
    final Map<String, byte[]> files = new HashMap<>();
    final List<String> fetched = new ArrayList<>();
    boolean offline;

    @Override
    public byte[] fetch(String url) throws IOException {
      fetched.add(url);
      if (offline) throw new IOException("offline");
      byte[] b = files.get(url);
      if (b == null) throw new IOException("404 " + url);
      return b;
    }

    void publish(String version, byte[] module, String rollout, long serial, byte[] seed) {
      publish(version, module, rollout, serial, seed, "");
    }

    void publish(String version, byte[] module, String rollout, long serial, byte[] seed, String extra) {
      files.put(BASE + "app-" + version + ".e7b", module);
      manifest("{\"version\":\"" + version + "\",\"url\":\"app-" + version + ".e7b\",\"sha256\":\""
          + E7ModuleManager.sha256Hex(module) + "\",\"rolloutPercent\":" + rollout + ",\"killSwitch\":false,\"serial\":"
          + serial + extra + "}", seed);
    }

    void manifest(String json, byte[] seed) {
      byte[] body = json.getBytes(StandardCharsets.UTF_8);
      files.put(MANIFEST_URL, body);
      files.put(MANIFEST_URL + ".sig", (E7ModuleManager.hex(E7VM.signDetached(body, seed)) + "\n").getBytes(StandardCharsets.US_ASCII));
    }
  }

  static final class Reports implements E7ModuleManager.ErrorReporter {
    final List<String> lines = new ArrayList<>();
    final List<Integer> statuses = new ArrayList<>();

    @Override
    public void report(String version, int status, String message) {
      lines.add(version + " | " + status + " | " + message);
      statuses.add(status);
    }

    void clear() {
      lines.clear();
      statuses.clear();
    }

    boolean has(String version, int status, String fragment) {
      for (String l : lines)
        if (l.startsWith(version + " | " + status + " | ") && l.contains(fragment)) return true;
      return false;
    }

    int count(int status) {
      int n = 0;
      for (int s : statuses) if (s == status) n++;
      return n;
    }
  }

  static final class Fixture implements AutoCloseable {
    final File dir;
    final Server server = new Server();
    final Reports reports = new Reports();
    final List<E7ModuleManager> managers = new ArrayList<>();
    String userId = "user-42";

    Fixture() throws IOException {
      dir = File.createTempFile("e7mm", "");
      if (!dir.delete() || !dir.mkdirs()) throw new IOException("cannot create " + dir);
    }

    E7ModuleManager manager() {
      E7ModuleManager m = new E7ModuleManager.Builder()
          .manifestUrl(MANIFEST_URL)
          .trustedKeys(E7VM.publicKey(SEED), E7VM.publicKey(ROTATED_SEED))
          .fetcher(server)
          .storageDir(dir)
          .userId(userId)
          .fallback(E7VM.compile("export function answer(): number { return 0; }", "fallback.ts"), "bundled")
          .vmConfig(new E7VM.Config().memoryLimit(16 << 20).gcGrowthPercent(150))
          .vmSetup(vm -> vm.setBudget(10_000_000))
          .errorReporter(reports)
          .maxFailures(3)
          .build();
      managers.add(m);
      return m;
    }

    @Override
    public void close() {
      for (E7ModuleManager m : managers) m.close();
      delete(dir);
    }

    static void delete(File f) {
      File[] kids = f.listFiles();
      if (kids != null) for (File k : kids) delete(k);
      f.delete();
    }
  }

  private ModuleManagerScenarios() {}
}
