package org.e7vm;

import java.nio.charset.StandardCharsets;

/**
 * Java / Kotlin binding of e7vm (Android and desktop JVM).
 *
 * <pre>
 * try (E7VM vm = new E7VM(new E7VM.Config().requireSignature(trustedKey))) {
 *   E7VM.Module m = vm.load(bytecode);
 *   String sig = (String) vm.call(m, "sign", "payload", new KeyHandle(1));
 * }
 * </pre>
 *
 * Argument types: Double/Float (number), Integer/Long/Short/Byte (integers), Boolean, String,
 * byte[] (Uint8Array), KeyHandle. Results: Double, Long, Boolean, String, byte[] or null (void).
 * A VM is not thread-safe; use one VM per thread.
 */
public final class E7VM implements AutoCloseable {
  static {
    NativeLoader.load();
  }

  /** Native function implemented in Java and callable from scripts via `declare function`. */
  public interface NativeFunction {
    Object invoke(Object[] args) throws Exception;
  }

  /** Supplies key material for {@link KeyHandle}s (e.g. from Android Keystore-protected storage). */
  public interface KeyProvider {
    byte[] key(long id);
  }

  public interface PrintHandler {
    void print(String line);
  }

  public static final class Config {
    long memoryLimit = 0;
    int stackSlots = 0;
    int maxFrames = 0;
    boolean suspendOnBudget = false;
    byte[] trustedKeys = null;
    int gcGrowthPercent = 0;

    Config copy() {
      Config c = new Config();
      c.memoryLimit = memoryLimit;
      c.stackSlots = stackSlots;
      c.maxFrames = maxFrames;
      c.suspendOnBudget = suspendOnBudget;
      c.trustedKeys = trustedKeys == null ? null : trustedKeys.clone();
      c.gcGrowthPercent = gcGrowthPercent;
      return c;
    }

    public Config memoryLimit(long bytes) { memoryLimit = bytes; return this; }
    /**
     * Next collection when the heap reaches this percentage of the live size after the previous one
     * (default 200, minimum 125; lower = less memory, more collections). 0 = default.
     */
    public Config gcGrowthPercent(int percent) { gcGrowthPercent = percent; return this; }
    public Config stackSlots(int n) { stackSlots = n; return this; }
    public Config maxFrames(int n) { maxFrames = n; return this; }
    public Config suspendOnBudget(boolean b) { suspendOnBudget = b; return this; }
    /** Require modules signed by one of these Ed25519 public keys (32 bytes each). */
    public Config requireSignature(byte[]... publicKeys) {
      byte[] all = new byte[32 * publicKeys.length];
      for (int i = 0; i < publicKeys.length; i++) {
        if (publicKeys[i].length != 32) throw new IllegalArgumentException("public keys must be 32 bytes");
        System.arraycopy(publicKeys[i], 0, all, 32 * i, 32);
      }
      trustedKeys = all;
      return this;
    }
  }

  /** Handle to a loaded module (owned by the VM). */
  public static final class Module {
    final long ptr;
    Module(long p) { ptr = p; }
  }

  /** Header of a module, read without loading it ({@link #inspect}). Mirrors e7_module_info. */
  public static final class ModuleInfo {
    /** Bytecode format version (this VM: 2). */
    public final int format;
    /** Oldest e7vm that can run the module: major &lt;&lt; 16 | minor &lt;&lt; 8 | patch. */
    public final int requiresVersion;
    /** An Ed25519 signature trailer is present (not verified by inspect). */
    public final boolean isSigned;
    /** First 8 bytes of SHA-256(public key) of the signing key (all zero when unsigned). */
    public final byte[] keyId;
    public final boolean hasDebugInfo;
    /** {@link E7Exception} status: 0 = this VM can load it, VERSION = format / version too new, LOAD = not a module. */
    public final int status;

    ModuleInfo(int format, int requiresVersion, boolean isSigned, byte[] keyId, boolean hasDebugInfo, int status) {
      this.format = format;
      this.requiresVersion = requiresVersion;
      this.isSigned = isSigned;
      this.keyId = keyId;
      this.hasDebugInfo = hasDebugInfo;
      this.status = status;
    }

    /** True if this VM supports the module's format and required version. */
    public boolean canRun() { return status == 0; }

    /** requiresVersion as text, e.g. "0.2.0". */
    public String requiresVersionString() { return versionString(requiresVersion); }
  }

  /** Garbage collector statistics since the VM was created ({@link #gcStats}). Mirrors e7_gc_stats. */
  public static final class GcStats {
    public final long collections;
    public final long totalPauseNanos;
    /** Longest single collection. */
    public final long maxPauseNanos;
    /** Currently allocated (same as {@link #memoryUsed}). */
    public final long heapBytes;
    public final long peakHeapBytes;

    GcStats(long[] v) {
      collections = v[0];
      totalPauseNanos = v[1];
      maxPauseNanos = v[2];
      heapBytes = v[3];
      peakHeapBytes = v[4];
    }

    @Override
    public String toString() {
      return "GcStats{collections=" + collections + ", totalPauseNanos=" + totalPauseNanos + ", maxPauseNanos="
          + maxPauseNanos + ", heapBytes=" + heapBytes + ", peakHeapBytes=" + peakHeapBytes + "}";
    }
  }

  private long handle;

  public E7VM() { this(new Config()); }

  public E7VM(Config cfg) {
    handle = nativeNew(cfg.memoryLimit, cfg.stackSlots, cfg.maxFrames, cfg.suspendOnBudget, cfg.trustedKeys,
        cfg.gcGrowthPercent);
    if (handle == 0) throw new E7Exception(E7Exception.MEMORY, "cannot create VM");
  }

  public Module load(byte[] bytecode) { return new Module(nativeLoad(check(), bytecode)); }

  /** Compiles and loads source code (requires a build with the compiler). */
  public Module loadSource(String source, String fileName) {
    return new Module(nativeLoadSource(check(), source.getBytes(StandardCharsets.UTF_8), fileName));
  }

  /** Calls an exported function. May throw E7Exception (status SUSPENDED if the budget ran out in suspend mode). */
  public Object call(Module module, String function, Object... args) {
    return nativeCall(check(), module.ptr, function, args);
  }

  /** Continues a suspended call. */
  public Object resume() { return nativeResume(check()); }

  public void abortSuspended() { nativeAbortSuspended(check()); }

  public void setBudget(long ticks) { nativeSetBudget(check(), ticks); }

  public void seedRandom(long seed) { nativeSeed(check(), seed); }

  public long memoryUsed() { return nativeMemoryUsed(check()); }

  public void collectGarbage() { nativeCollectGarbage(check()); }

  /** Garbage collector statistics (count, pause times, heap size and peak) for sizing memory limits. */
  public GcStats gcStats() { return new GcStats(nativeGcStats(check())); }

  /** signature: see e7.h, e.g. "sn>s" (string, number) -> string. Register before load(). */
  public void registerNative(String name, String signature, NativeFunction fn) {
    nativeRegister(check(), name, signature, fn);
  }

  public void setKeyProvider(KeyProvider provider) { nativeSetKeyProvider(check(), provider); }

  public void setPrintHandler(PrintHandler handler) { nativeSetPrint(check(), handler); }

  public boolean hasFunction(Module module, String function) { return nativeHasFunction(module.ptr, function); }

  /** Compiles source to .e7b bytecode. */
  public static byte[] compile(String source, String fileName) {
    return nativeCompile(source.getBytes(StandardCharsets.UTF_8), fileName);
  }

  /** Signs bytecode with an Ed25519 seed (32 bytes); normally done on the build server. */
  public static byte[] sign(byte[] bytecode, byte[] seed) { return nativeSign(bytecode, seed); }

  public static byte[] publicKey(byte[] seed) { return nativePublicKey(seed); }

  /** Library version, e.g. "0.2.0". */
  public static String version() { return versionString(versionNumber()); }

  /** Library version as major &lt;&lt; 16 | minor &lt;&lt; 8 | patch (compare with {@link ModuleInfo#requiresVersion}). */
  public static int versionNumber() { return nativeVersionNumber(); }

  /** Reads a module's header without loading it, e.g. to skip modules this VM cannot run. Never throws for bad input. */
  public static ModuleInfo inspect(byte[] bytecode) {
    byte[] keyId = new byte[8];
    long[] v = nativeInspect(bytecode, keyId);
    return new ModuleInfo((int) v[1], (int) v[2], v[3] != 0, keyId, v[4] != 0, (int) v[0]);
  }

  /**
   * Detached Ed25519 signature (64 bytes) of arbitrary data, e.g. an update manifest (e7_sign_detached).
   * Normally done on the build server ({@code e7c --sign-file}).
   */
  public static byte[] signDetached(byte[] data, byte[] seed) { return nativeSignDetached(data, seed); }

  /** True if {@code signature} is a detached signature of {@code data} by one of the public keys (32 bytes each). */
  public static boolean verifyDetached(byte[] data, byte[] signature, byte[]... publicKeys) {
    if (signature == null || signature.length != 64 || publicKeys.length == 0) return false;
    byte[] all = new byte[32 * publicKeys.length];
    for (int i = 0; i < publicKeys.length; i++) {
      if (publicKeys[i] == null || publicKeys[i].length != 32) throw new IllegalArgumentException("public keys must be 32 bytes");
      System.arraycopy(publicKeys[i], 0, all, 32 * i, 32);
    }
    return nativeVerifyDetached(data, signature, all);
  }

  static String versionString(int v) { return (v >>> 16) + "." + ((v >>> 8) & 0xFF) + "." + (v & 0xFF); }

  @Override
  public synchronized void close() {
    if (handle != 0) {
      nativeFree(handle);
      handle = 0;
    }
  }

  private long check() {
    if (handle == 0) throw new IllegalStateException("VM is closed");
    return handle;
  }

  private static native long nativeNew(long mem, int stack, int frames, boolean suspend, byte[] trusted, int gcGrowth);
  private static native void nativeFree(long vm);
  private static native long nativeLoad(long vm, byte[] code);
  private static native long nativeLoadSource(long vm, byte[] source, String name);
  private static native Object nativeCall(long vm, long module, String fn, Object[] args);
  private static native Object nativeResume(long vm);
  private static native void nativeAbortSuspended(long vm);
  private static native void nativeSetBudget(long vm, long ticks);
  private static native void nativeSeed(long vm, long seed);
  private static native long nativeMemoryUsed(long vm);
  private static native void nativeCollectGarbage(long vm);
  private static native long[] nativeGcStats(long vm);
  private static native void nativeRegister(long vm, String name, String sig, NativeFunction fn);
  private static native void nativeSetKeyProvider(long vm, KeyProvider p);
  private static native void nativeSetPrint(long vm, PrintHandler h);
  private static native boolean nativeHasFunction(long module, String fn);
  private static native byte[] nativeCompile(byte[] source, String name);
  private static native byte[] nativeSign(byte[] code, byte[] seed);
  private static native byte[] nativePublicKey(byte[] seed);
  private static native int nativeVersionNumber();
  private static native long[] nativeInspect(byte[] code, byte[] keyIdOut);
  private static native byte[] nativeSignDetached(byte[] data, byte[] seed);
  private static native boolean nativeVerifyDetached(byte[] data, byte[] sig, byte[] keys);
}
