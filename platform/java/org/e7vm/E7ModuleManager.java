package org.e7vm;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.Set;

/**
 * Delivers E7 modules over the air with a signed manifest, staged rollout, kill switch and rollback
 * (Java, Kotlin and Android). See docs/DEPLOYMENT.md.
 *
 * <pre>
 * E7ModuleManager mm = new E7ModuleManager.Builder()
 *     .manifestUrl("https://updates.example.com/pricing/manifest.json")
 *     .trustedKeys(releaseKey, nextReleaseKey)          // Ed25519 public keys (32 bytes)
 *     .fetcher(url -&gt; httpGet(url))                     // the app's HTTP stack
 *     .storageDir(new File(context.getFilesDir(), "e7"))
 *     .userId(installationId)                           // stable per user / install
 *     .fallback(bundledModule, "bundled-1.4")           // shipped inside the app
 *     .vmConfig(new E7VM.Config().memoryLimit(16 &lt;&lt; 20))
 *     .vmSetup(vm -&gt; { vm.setBudget(5_000_000); vm.registerNative(...); })
 *     .errorReporter((version, status, message) -&gt; telemetry.log(version, status, message))
 *     .build();
 * E7ModuleManager.Active a = mm.start();     // at launch: last-known-good from disk, or the fallback
 * a = mm.update();                            // in the background: manifest, download, verify, activate
 * Object price = mm.active().call("price", order);
 * mm.reportSuccess();                         // or mm.reportFailure(status, message)
 * </pre>
 *
 * <p>The manifest ({@code manifest.json}) is a JSON object {@code {version, url, sha256, rolloutPercent,
 * killSwitch, minVm?, serial?}} with a detached signature in {@code manifest.json.sig} (hex, made by
 * {@code e7c --sign-file} or {@code tools/manifest.py}). A manifest whose signature does not verify against
 * the trusted keys is ignored, so a kill switch or a rollout cannot be forged. Downloaded modules must be
 * signed by a trusted key too; the bundled fallback is not checked (it is part of the app).
 *
 * <p>The manager owns the VMs it creates: a replaced VM is closed. Like a VM, use the manager from one
 * thread at a time (its methods are synchronized; run {@link #update} on the thread that calls the module,
 * or make sure no call is running while it switches modules).
 */
public final class E7ModuleManager {
  /** Downloads a URL (manifest, signature, module) with the app's own networking stack. */
  public interface Fetcher {
    byte[] fetch(String url) throws Exception;
  }

  /** Prepares every new VM before the module loads: natives, key provider, print handler, budget. */
  public interface VmSetup {
    void setUp(E7VM vm) throws Exception;
  }

  /**
   * Receives problems the manager handled (signature / checksum failures, modules that do not load,
   * selfTest failures, rollbacks). moduleVersion is null when no version is known (e.g. a bad manifest);
   * status is an {@link E7Exception} status code.
   */
  public interface ErrorReporter {
    void report(String moduleVersion, int status, String message);
  }

  /** The module currently in use. */
  public static final class Active {
    public final E7VM vm;
    public final E7VM.Module module;
    /** Version from the manifest, or the fallback's version label. */
    public final String version;
    /** Hex SHA-256 of the module file; null for the fallback. */
    public final String sha256;
    public final boolean isFallback;

    Active(E7VM vm, E7VM.Module module, String version, String sha256, boolean isFallback) {
      this.vm = vm;
      this.module = module;
      this.version = version;
      this.sha256 = sha256;
      this.isFallback = isFallback;
    }

    /** Calls an exported function of the active module. */
    public Object call(String function, Object... args) { return vm.call(module, function, args); }
  }

  public static final class Builder {
    private String manifestUrl;
    private final List<byte[]> trustedKeys = new ArrayList<>();
    private Fetcher fetcher;
    private File storageDir;
    private String userId;
    private byte[] fallbackModule;
    private String fallbackVersion = "fallback";
    private E7VM.Config vmConfig = new E7VM.Config();
    private VmSetup vmSetup;
    private ErrorReporter errorReporter;
    private int maxFailures = 3;
    private String selfTestFunction = "selfTest";

    public Builder manifestUrl(String url) { manifestUrl = url; return this; }
    /** Ed25519 public keys (32 bytes each) trusted for manifests and modules; several during a key rotation. */
    public Builder trustedKeys(byte[]... keys) { trustedKeys.addAll(Arrays.asList(keys)); return this; }
    public Builder fetcher(Fetcher f) { fetcher = f; return this; }
    /** Private directory for the last-known-good modules and the manager's state. */
    public Builder storageDir(File dir) { storageDir = dir; return this; }
    /** Stable id of the user or installation; decides the rollout bucket. */
    public Builder userId(String id) { userId = id; return this; }
    /** The module bundled with the app: used on a kill switch or when no downloaded module is usable. */
    public Builder fallback(byte[] module, String version) { fallbackModule = module; fallbackVersion = version; return this; }
    /** Memory limit, GC and suspension settings of the VMs (trusted keys are set by the manager). */
    public Builder vmConfig(E7VM.Config cfg) { vmConfig = cfg; return this; }
    public Builder vmSetup(VmSetup s) { vmSetup = s; return this; }
    public Builder errorReporter(ErrorReporter r) { errorReporter = r; return this; }
    /** Consecutive failures (reportFailure) after which a downloaded module is rolled back (default 3). */
    public Builder maxFailures(int n) { maxFailures = n; return this; }
    /** Exported function that must return true before a downloaded module is activated (null: none). */
    public Builder selfTestFunction(String name) { selfTestFunction = name; return this; }

    public E7ModuleManager build() {
      if (manifestUrl == null || fetcher == null || storageDir == null || userId == null || fallbackModule == null)
        throw new IllegalArgumentException("manifestUrl, fetcher, storageDir, userId and fallback are required");
      if (trustedKeys.isEmpty()) throw new IllegalArgumentException("at least one trusted key is required");
      for (byte[] k : trustedKeys)
        if (k == null || k.length != 32) throw new IllegalArgumentException("public keys must be 32 bytes");
      if (maxFailures < 1) throw new IllegalArgumentException("maxFailures must be at least 1");
      return new E7ModuleManager(this);
    }
  }

  // ------------------------------------------------------------------ state
  private final String manifestUrl;
  private final byte[][] trustedKeys;
  private final Fetcher fetcher;
  private final String userId;
  private final byte[] fallbackModule;
  private final String fallbackVersion;
  private final E7VM.Config vmConfig;
  private final VmSetup vmSetup;
  private final ErrorReporter errorReporter;
  private final int maxFailures;
  private final String selfTestFunction;
  private final File dir, modulesDir, stateFile;

  /** Persistent state (state.properties). */
  private static final class Entry {
    final String version, sha256;
    Entry(String version, String sha256) { this.version = version; this.sha256 = sha256; }
  }
  private Entry current, previous;  // last-known-good and the one before it
  private int failures;
  private boolean killed;
  private long serial = Long.MIN_VALUE;  // newest manifest serial accepted
  private final Set<String> bad = new LinkedHashSet<>();  // sha256 of rolled-back / broken modules

  private Active active;

  private E7ModuleManager(Builder b) {
    manifestUrl = b.manifestUrl;
    trustedKeys = b.trustedKeys.toArray(new byte[0][]);
    fetcher = b.fetcher;
    userId = b.userId;
    fallbackModule = b.fallbackModule;
    fallbackVersion = b.fallbackVersion;
    vmConfig = b.vmConfig.copy();
    vmSetup = b.vmSetup;
    errorReporter = b.errorReporter;
    maxFailures = b.maxFailures;
    selfTestFunction = b.selfTestFunction;
    dir = b.storageDir;
    modulesDir = new File(dir, "modules");
    stateFile = new File(dir, "state.properties");
    loadState();
  }

  /** The module in use (null before {@link #start} / {@link #update}). */
  public synchronized Active active() { return active; }

  /**
   * Activates the last-known-good module from disk without network access (or the fallback when there
   * is none, it no longer loads, or a kill switch is in effect). Call at app start.
   */
  public synchronized Active start() {
    activateFromDisk();
    return active;
  }

  /**
   * Fetches and verifies the manifest; downloads, verifies, self-tests and activates the module it names
   * when this client is in the rollout. On any problem the current module stays active (the problem goes
   * to the error reporter). Never throws for remote problems; blocks while downloading.
   */
  public synchronized Active update() {
    if (active == null) activateFromDisk();
    Manifest mf;
    try {
      byte[] body = fetcher.fetch(manifestUrl);
      byte[] sig = parseSignature(fetcher.fetch(manifestUrl + ".sig"));
      if (body == null || sig == null || !E7VM.verifyDetached(body, sig, trustedKeys)) {
        report(null, E7Exception.SIGNATURE, "manifest signature is not valid for the trusted keys");
        return active;
      }
      mf = Manifest.parse(body);
    } catch (Exception e) {
      report(null, E7Exception.LOAD, "manifest: " + describe(e));
      return active;
    }
    // replay protection: once a manifest with a serial was accepted, older (or serial-less) ones are ignored
    if (serial != Long.MIN_VALUE && (mf.serial == null || mf.serial < serial)) {
      report(mf.version, E7Exception.SIGNATURE, "stale manifest ignored (serial "
          + (mf.serial == null ? "missing" : mf.serial.toString()) + ", newest accepted " + serial + ")");
      return active;
    }
    if (mf.serial != null) serial = mf.serial;
    killed = mf.killSwitch;
    saveState();
    if (mf.killSwitch) {
      if (!active.isFallback) activateFallback();
      return active;
    }
    if (rolloutBucket(userId, mf.version) >= Math.round(mf.rolloutPercent * 100)) return keepCurrent();
    if (mf.minVm > E7VM.versionNumber()) {
      report(mf.version, E7Exception.VERSION, "module requires e7vm " + E7VM.versionString(mf.minVm) + ", this is "
          + E7VM.version());
      return keepCurrent();
    }
    if (bad.contains(mf.sha256)) return keepCurrent();  // rolled back before: wait for a new version
    if (active != null && !active.isFallback && mf.sha256.equals(active.sha256)) return active;  // up to date
    byte[] code = readCached(mf.sha256);
    if (code == null) {
      try {
        code = fetcher.fetch(URI.create(manifestUrl).resolve(mf.url).toString());
      } catch (Exception e) {
        report(mf.version, E7Exception.LOAD, "download failed: " + describe(e));
        return keepCurrent();
      }
      String got = code == null ? "nothing" : sha256Hex(code);
      if (!mf.sha256.equals(got)) {
        report(mf.version, E7Exception.SIGNATURE, "sha256 mismatch: manifest " + mf.sha256 + ", downloaded " + got);
        return keepCurrent();
      }
    }
    Active next = instantiateChecked(code, mf.version, mf.sha256);
    if (next == null) return keepCurrent();
    try {
      writeAtomically(new File(modulesDir, mf.sha256 + ".e7b"), code);
    } catch (IOException e) {
      report(mf.version, E7Exception.STATE, "cannot store the module: " + describe(e));
    }
    if (current != null && !current.sha256.equals(mf.sha256)) previous = current;
    current = new Entry(mf.version, mf.sha256);
    failures = 0;
    saveState();
    switchTo(next);
    return active;
  }

  /** The host saw the active module work (e.g. a call returned a valid result): resets the failure count. */
  public synchronized void reportSuccess() {
    if (failures != 0) {
      failures = 0;
      saveState();
    }
  }

  /**
   * The host saw the active module fail (an exception, a result it had to reject). After maxFailures
   * consecutive failures a downloaded module is rolled back to the previous good one, or the fallback,
   * and is not activated again.
   */
  public synchronized void reportFailure(int status, String message) {
    if (active == null || active.isFallback) return;
    failures++;
    if (failures < maxFailures) {
      saveState();
      return;
    }
    report(active.version, status, "rolled back after " + failures + " consecutive failures: " + message);
    markBad(active.sha256);
    failures = 0;
    saveState();
    activateFromDisk();
  }

  /** Closes the active VM. */
  public synchronized void close() {
    if (active != null) active.vm.close();
    active = null;
  }

  /**
   * Rollout bucket 0..9999 of a user for a module version: the first 4 bytes of
   * SHA-256(UTF-8 of version + ":" + userId) as a big-endian unsigned number, modulo 10000. A client takes
   * the version when bucket &lt; rolloutPercent * 100.
   */
  public static int rolloutBucket(String userId, String version) {
    byte[] h = sha256((version + ":" + userId).getBytes(StandardCharsets.UTF_8));
    long v = ((h[0] & 0xFFL) << 24) | ((h[1] & 0xFFL) << 16) | ((h[2] & 0xFFL) << 8) | (h[3] & 0xFFL);
    return (int) (v % 10000);
  }

  // ------------------------------------------------------------------ activation
  private Active keepCurrent() {
    if (active == null || active.isFallback) activateFromDisk();
    return active;
  }

  // Last-known-good, then the previous module, then the fallback. Unusable entries are dropped.
  private void activateFromDisk() {
    if (!killed) {
      for (int i = 0; i < 2; i++) {
        Entry e = current;
        if (e == null) break;
        if (active != null && !active.isFallback && e.sha256.equals(active.sha256)) return;
        Active a = null;
        if (!bad.contains(e.sha256)) {
          byte[] code = readCached(e.sha256);
          if (code == null) report(e.version, E7Exception.LOAD, "stored module is missing or corrupted");
          else a = instantiateChecked(code, e.version, e.sha256);
          if (a == null) markBad(e.sha256);
        }
        if (a != null) {
          saveState();
          switchTo(a);
          return;
        }
        current = previous;  // drop the unusable module
        previous = null;
        failures = 0;
        saveState();
      }
    }
    if (active == null || !active.isFallback) activateFallback();
  }

  private void activateFallback() {
    E7VM vm = null;
    try {
      vm = newVm(false);
      E7VM.Module m = vm.load(fallbackModule);
      switchTo(new Active(vm, m, fallbackVersion, null, true));
    } catch (Exception e) {
      if (vm != null) vm.close();
      int st = e instanceof E7Exception ? ((E7Exception) e).status : E7Exception.STATE;
      report(fallbackVersion, st, "the fallback module does not load: " + describe(e));
      throw e instanceof E7Exception ? (E7Exception) e : new E7Exception(st, describe(e));
    }
  }

  // inspect + signed load + selfTest; reports and returns null on failure
  private Active instantiateChecked(byte[] code, String version, String sha) {
    E7VM.ModuleInfo info = E7VM.inspect(code);
    if (!info.canRun()) {
      report(version, info.status, info.status == E7Exception.VERSION
          ? "module needs e7vm " + info.requiresVersionString() + " (format " + info.format + "), this is " + E7VM.version()
          : "not a valid e7 module");
      return null;
    }
    E7VM vm = null;
    try {
      vm = newVm(true);
      E7VM.Module m = vm.load(code);
      if (selfTestFunction != null && vm.hasFunction(m, selfTestFunction)) {
        Object r = vm.call(m, selfTestFunction);
        if (!Boolean.TRUE.equals(r)) throw new E7Exception(E7Exception.RUNTIME, selfTestFunction + "() returned " + r);
      }
      return new Active(vm, m, version, sha, false);
    } catch (Exception e) {
      if (vm != null) vm.close();
      report(version, e instanceof E7Exception ? ((E7Exception) e).status : E7Exception.RUNTIME, describe(e));
      return null;
    }
  }

  private E7VM newVm(boolean requireSignature) throws Exception {
    E7VM.Config c = vmConfig.copy();
    c.trustedKeys = null;
    if (requireSignature) c.requireSignature(trustedKeys);
    E7VM vm = new E7VM(c);
    try {
      if (vmSetup != null) vmSetup.setUp(vm);
    } catch (Exception e) {
      vm.close();
      throw e;
    }
    return vm;
  }

  private void switchTo(Active next) {
    Active old = active;
    active = next;
    if (old != null && old != next) old.vm.close();
    cleanModules();
  }

  private void markBad(String sha) {
    bad.add(sha);
    while (bad.size() > 32) bad.remove(bad.iterator().next());
    if (current != null && current.sha256.equals(sha) && active != null && sha.equals(active.sha256)) {
      current = previous;
      previous = null;
    } else if (previous != null && previous.sha256.equals(sha)) {
      previous = null;
    }
  }

  private void report(String version, int status, String message) {
    if (errorReporter == null) return;
    try {
      errorReporter.report(version, status, message);
    } catch (RuntimeException ignored) {
      // a failing reporter must not break module management
    }
  }

  private static String describe(Throwable e) {
    String m = e.getMessage();
    return m == null || m.isEmpty() ? e.getClass().getSimpleName() : m;
  }

  // ------------------------------------------------------------------ storage
  private byte[] readCached(String sha) {
    File f = new File(modulesDir, sha + ".e7b");
    if (!f.isFile()) return null;
    try {
      byte[] b = readFile(f);
      return sha.equals(sha256Hex(b)) ? b : null;
    } catch (IOException e) {
      return null;
    }
  }

  private void cleanModules() {
    File[] files = modulesDir.listFiles();
    if (files == null) return;
    for (File f : files) {
      String n = f.getName();
      boolean keep = (current != null && n.equals(current.sha256 + ".e7b")) || (previous != null && n.equals(previous.sha256 + ".e7b"));
      if (!keep) f.delete();
    }
  }

  private void loadState() {
    if (!stateFile.isFile()) return;
    Properties p = new Properties();
    try (InputStream in = new FileInputStream(stateFile)) {
      p.load(in);
    } catch (IOException e) {
      return;
    }
    current = entry(p, "current");
    previous = entry(p, "previous");
    failures = parseInt(p.getProperty("failures"));
    killed = "true".equals(p.getProperty("killed"));
    String s = p.getProperty("serial");
    if (s != null) {
      try {
        serial = Long.parseLong(s);
      } catch (NumberFormatException ignored) {
        // keep the default
      }
    }
    String b = p.getProperty("bad", "");
    for (String x : b.split(",")) if (!x.isEmpty()) bad.add(x);
  }

  private static Entry entry(Properties p, String key) {
    String v = p.getProperty(key + ".version"), h = p.getProperty(key + ".sha256");
    return v != null && h != null && h.matches("[0-9a-f]{64}") ? new Entry(v, h) : null;
  }

  private static int parseInt(String s) {
    try {
      return s == null ? 0 : Integer.parseInt(s);
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  private void saveState() {
    Properties p = new Properties();
    if (current != null) {
      p.setProperty("current.version", current.version);
      p.setProperty("current.sha256", current.sha256);
    }
    if (previous != null) {
      p.setProperty("previous.version", previous.version);
      p.setProperty("previous.sha256", previous.sha256);
    }
    p.setProperty("failures", Integer.toString(failures));
    p.setProperty("killed", Boolean.toString(killed));
    if (serial != Long.MIN_VALUE) p.setProperty("serial", Long.toString(serial));
    StringBuilder b = new StringBuilder();
    for (String x : bad) b.append(b.length() == 0 ? "" : ",").append(x);
    p.setProperty("bad", b.toString());
    try {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      p.store(out, "E7ModuleManager state");
      writeAtomically(stateFile, out.toByteArray());
    } catch (IOException e) {
      report(null, E7Exception.STATE, "cannot save the module state: " + describe(e));
    }
  }

  private static void writeAtomically(File f, byte[] data) throws IOException {
    File parent = f.getParentFile();
    if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
    File tmp = new File(parent, f.getName() + ".tmp");
    try (FileOutputStream out = new FileOutputStream(tmp)) {
      out.write(data);
      out.getFD().sync();
    }
    if (!tmp.renameTo(f)) {  // Windows: rename does not replace
      f.delete();
      if (!tmp.renameTo(f)) throw new IOException("cannot write " + f);
    }
  }

  private static byte[] readFile(File f) throws IOException {
    try (InputStream in = new FileInputStream(f)) {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buf = new byte[16384];
      for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
      return out.toByteArray();
    }
  }

  // ------------------------------------------------------------------ hashing, signatures
  static byte[] sha256(byte[] data) {
    try {
      return MessageDigest.getInstance("SHA-256").digest(data);
    } catch (NoSuchAlgorithmException e) {
      throw new IllegalStateException(e);
    }
  }

  static String sha256Hex(byte[] data) { return hex(sha256(data)); }

  static String hex(byte[] b) {
    StringBuilder s = new StringBuilder(b.length * 2);
    for (byte x : b) s.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
    return s.toString();
  }

  // manifest.json.sig: 128 hex digits (e7c --sign-file), surrounding whitespace allowed; or 64 raw bytes
  static byte[] parseSignature(byte[] raw) {
    if (raw == null) return null;
    if (raw.length == 64) return raw.clone();
    String s = new String(raw, StandardCharsets.US_ASCII).trim();
    if (s.length() != 128) return null;
    byte[] out = new byte[64];
    for (int i = 0; i < 64; i++) {
      int hi = Character.digit(s.charAt(2 * i), 16), lo = Character.digit(s.charAt(2 * i + 1), 16);
      if (hi < 0 || lo < 0) return null;
      out[i] = (byte) ((hi << 4) | lo);
    }
    return out;
  }

  // ------------------------------------------------------------------ manifest
  static final class Manifest {
    String version, url, sha256;
    double rolloutPercent;
    boolean killSwitch;
    int minVm;
    Long serial;

    static Manifest parse(byte[] body) {
      Object root = new Json(new String(body, StandardCharsets.UTF_8)).parseDocument();
      if (!(root instanceof Map)) throw new IllegalArgumentException("not a JSON object");
      Map<?, ?> o = (Map<?, ?>) root;
      Manifest m = new Manifest();
      Object k = o.get("killSwitch");
      if (k != null && !(k instanceof Boolean)) throw new IllegalArgumentException("killSwitch must be a boolean");
      m.killSwitch = Boolean.TRUE.equals(k);
      Object s = o.get("serial");
      if (s != null) {
        if (!(s instanceof Double) || ((Double) s) != Math.rint((Double) s)) throw new IllegalArgumentException("serial must be an integer");
        m.serial = ((Double) s).longValue();
      }
      m.version = o.get("version") instanceof String ? (String) o.get("version") : null;
      if (m.killSwitch) return m;
      if (m.version == null || m.version.isEmpty()) throw new IllegalArgumentException("missing version");
      if (!(o.get("url") instanceof String)) throw new IllegalArgumentException("missing url");
      m.url = (String) o.get("url");
      Object h = o.get("sha256");
      if (!(h instanceof String) || !((String) h).toLowerCase().matches("[0-9a-f]{64}"))
        throw new IllegalArgumentException("sha256 must be 64 hex digits");
      m.sha256 = ((String) h).toLowerCase();
      Object r = o.get("rolloutPercent");
      if (!(r instanceof Double) || (Double) r < 0 || (Double) r > 100)
        throw new IllegalArgumentException("rolloutPercent must be a number from 0 to 100");
      m.rolloutPercent = (Double) r;
      Object v = o.get("minVm");
      if (v instanceof String) m.minVm = parseVersion((String) v);
      else if (v instanceof Double) m.minVm = ((Double) v).intValue();
      else if (v != null) throw new IllegalArgumentException("minVm must be a version string");
      return m;
    }

    // "0.2", "0.2.1", "v0.2" -> major << 16 | minor << 8 | patch
    static int parseVersion(String s) {
      String t = s.startsWith("v") ? s.substring(1) : s;
      String[] p = t.split("\\.", -1);
      if (p.length < 2 || p.length > 3) throw new IllegalArgumentException("invalid version " + s);
      int v = 0;
      for (int i = 0; i < 3; i++) {
        int x = 0;
        if (i < p.length) {
          if (!p[i].matches("[0-9]{1,3}") || (x = Integer.parseInt(p[i])) > 255) throw new IllegalArgumentException("invalid version " + s);
        }
        v = (v << 8) | x;
      }
      return v;
    }
  }

  /** Minimal JSON reader (objects, arrays, strings, numbers as Double, booleans, null). */
  static final class Json {
    private final String s;
    private int i;

    Json(String s) { this.s = s; }

    Object parseDocument() {
      Object v = value(0);
      ws();
      if (i != s.length()) throw err("trailing characters");
      return v;
    }

    private IllegalArgumentException err(String what) { return new IllegalArgumentException("invalid JSON at " + i + ": " + what); }

    private void ws() {
      while (i < s.length() && " \t\r\n".indexOf(s.charAt(i)) >= 0) i++;
    }

    private Object value(int depth) {
      if (depth > 64) throw err("nested too deeply");
      ws();
      if (i >= s.length()) throw err("unexpected end");
      char c = s.charAt(i);
      if (c == '{') {
        i++;
        Map<String, Object> o = new LinkedHashMap<>();
        ws();
        if (i < s.length() && s.charAt(i) == '}') { i++; return o; }
        while (true) {
          ws();
          if (i >= s.length() || s.charAt(i) != '"') throw err("expected a key");
          String k = string();
          ws();
          if (i >= s.length() || s.charAt(i) != ':') throw err("expected ':'");
          i++;
          o.put(k, value(depth + 1));
          ws();
          if (i < s.length() && s.charAt(i) == ',') { i++; continue; }
          if (i < s.length() && s.charAt(i) == '}') { i++; return o; }
          throw err("expected ',' or '}'");
        }
      }
      if (c == '[') {
        i++;
        List<Object> a = new ArrayList<>();
        ws();
        if (i < s.length() && s.charAt(i) == ']') { i++; return a; }
        while (true) {
          a.add(value(depth + 1));
          ws();
          if (i < s.length() && s.charAt(i) == ',') { i++; continue; }
          if (i < s.length() && s.charAt(i) == ']') { i++; return a; }
          throw err("expected ',' or ']'");
        }
      }
      if (c == '"') return string();
      if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
      if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
      if (s.startsWith("null", i)) { i += 4; return null; }
      int start = i;
      while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
      if (start == i) throw err("unexpected character");
      try {
        return Double.valueOf(s.substring(start, i));
      } catch (NumberFormatException e) {
        throw err("bad number");
      }
    }

    private String string() {
      i++;  // opening quote
      StringBuilder b = new StringBuilder();
      while (true) {
        if (i >= s.length()) throw err("unterminated string");
        char c = s.charAt(i++);
        if (c == '"') return b.toString();
        if (c != '\\') { b.append(c); continue; }
        if (i >= s.length()) throw err("unterminated string");
        char e = s.charAt(i++);
        switch (e) {
          case '"': case '\\': case '/': b.append(e); break;
          case 'b': b.append('\b'); break;
          case 'f': b.append('\f'); break;
          case 'n': b.append('\n'); break;
          case 'r': b.append('\r'); break;
          case 't': b.append('\t'); break;
          case 'u':
            if (i + 4 > s.length()) throw err("bad escape");
            try {
              b.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
            } catch (NumberFormatException ex) {
              throw err("bad escape");
            }
            i += 4;
            break;
          default: throw err("bad escape");
        }
      }
    }
  }
}
