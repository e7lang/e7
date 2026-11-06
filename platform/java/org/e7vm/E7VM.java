package org.e7vm;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

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
 *
 * <p>Async host functions: a script's {@code declare function fetchKey(id: string): Promise<Uint8Array>;}
 * is registered with {@link #registerAsyncNative}; the host returns a {@link CompletionStage} completed on any
 * thread. A call that waits for such operations runs with {@link #callAsync} (non-blocking: the VM runs on an
 * executor, the "VM thread") or {@link #callAwait} (blocking: the calling thread is the VM thread):
 *
 * <pre>
 * ExecutorService vmThread = Executors.newSingleThreadExecutor();
 * vm.registerAsyncNative("fetchKey", "s&gt;y", args -&gt; keyStore.fetchAsync((String) args[0]));
 * CompletableFuture&lt;Object&gt; sig = vm.callAsync(vmThread, m, "sign", "k1", payload);
 * </pre>
 */
public final class E7VM implements AutoCloseable {
  static {
    NativeLoader.load();
  }

  /** Native function implemented in Java and callable from scripts via `declare function`. */
  public interface NativeFunction {
    Object invoke(Object[] args) throws Exception;
  }

  /**
   * Async host function ({@link #registerAsyncNative}). Returns a stage (e.g. a {@link CompletableFuture})
   * completed, on any thread, with the value of the script's promise (null for void); completing it
   * exceptionally rejects the promise with an Error carrying the exception's message. An exception thrown by
   * {@code invoke} itself throws in the script at the call, as for a {@link NativeFunction}.
   */
  public interface AsyncNativeFunction {
    CompletionStage<?> invoke(Object[] args) throws Exception;
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

    /** requiresVersion as text, e.g. "0.1.0". */
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
  /** The async call in progress (callAsync / callAwait / loadAsync / loadAwait) until it finishes. */
  private volatile AsyncCall<?> current;

  public E7VM() { this(new Config()); }

  public E7VM(Config cfg) {
    handle = nativeNew(cfg.memoryLimit, cfg.stackSlots, cfg.maxFrames, cfg.suspendOnBudget, cfg.trustedKeys,
        cfg.gcGrowthPercent);
    if (handle == 0) throw new E7Exception(E7Exception.MEMORY, "cannot create VM");
  }

  /**
   * Loads a module and runs its top-level code. If the top level awaits an async host function that does not
   * complete at once, throws E7Exception PENDING (the initializer is abandoned): use {@link #loadAsync} /
   * {@link #loadAwait} for such modules.
   */
  public Module load(byte[] bytecode) {
    long h = check();
    try {
      return new Module(nativeLoad(h, bytecode));
    } catch (E7Exception e) {
      throw abandonPending(h, e, "the module's top-level code");
    }
  }

  /** Compiles and loads source code (requires a build with the compiler). */
  public Module loadSource(String source, String fileName) {
    long h = check();
    try {
      return new Module(nativeLoadSource(h, source.getBytes(StandardCharsets.UTF_8), fileName));
    } catch (E7Exception e) {
      throw abandonPending(h, e, "the module's top-level code");
    }
  }

  /**
   * Calls an exported function. May throw E7Exception (status SUSPENDED if the budget ran out in suspend mode;
   * PENDING if the call waits for an async host function: it is abandoned, run it with {@link #callAsync} or
   * {@link #callAwait}). An async export ({@code async function f(): Promise<T>}) returns T when no host
   * operation is left open, e.g. when every async native it called completed at once.
   */
  public Object call(Module module, String function, Object... args) {
    long h = check();
    try {
      return nativeCall(h, module.ptr, function, args);
    } catch (E7Exception e) {
      throw abandonPending(h, e, "'" + function + "'");
    }
  }

  /** Continues a suspended call. */
  public Object resume() {
    long h = check();
    try {
      return nativeResume(h);
    } catch (E7Exception e) {
      throw abandonPending(h, e, "the call");
    }
  }

  /** Abandons a suspended call, or the async call in progress (its future fails with a CancellationException). */
  public void abortSuspended() {
    long h = check();
    AsyncCall<?> c = current;
    if (c != null) c.abandon(new CancellationException("the call was abandoned (abortSuspended)"));
    else nativeAbortSuspended(h);
  }

  // A synchronous call that returned E7_PENDING: nothing would settle its tickets, so it is abandoned.
  private static E7Exception abandonPending(long h, E7Exception e, String what) {
    if (e.status != E7Exception.PENDING) return e;
    nativeAbortSuspended(h);
    nativePendingModule(h);
    return new E7Exception(E7Exception.PENDING, what + " waits for an async host function, so it was abandoned: "
        + "run it with callAsync / callAwait (loadAsync / loadAwait)");
  }

  // ------------------------------------------------------------------ async host functions

  /**
   * Registers an async host function (Android: API 24+, for {@link CompletionStage}) before load(): the script declares it returning a Promise, e.g.
   * {@code declare function fetchKey(id: string): Promise<Uint8Array>;} with signature "s&gt;y" (the return
   * code is the promise's value type). Each call runs {@code fn} on the VM's thread; the returned stage may
   * complete on any thread. A stage that is already complete settles the promise at once (then even
   * {@link #call} works); otherwise the completion goes to the VM thread of the {@link #callAsync} /
   * {@link #callAwait} in progress, which settles the promises in the order the operations complete.
   */
  public void registerAsyncNative(String name, String signature, AsyncNativeFunction fn) {
    if (fn == null) throw new IllegalArgumentException("fn is null");
    nativeRegisterAsync(check(), name, signature, new AsyncBridge(this, name, fn));
  }

  /**
   * Calls an exported function asynchronously. The call and the completions of async host functions run
   * on {@code vmThread}, which must run tasks one at a time and in order: e.g.
   * {@code Executors.newSingleThreadExecutor()}, Android's main thread ({@code context.getMainExecutor()}) or
   * {@code handler::post}; use the VM only from that executor. The future completes with the result (for an
   * async export, the promise's value) or fails with an E7Exception; cancelling it abandons the call. While
   * the call waits for host operations, other calls fail with E7Exception STATE. Android: API 24+.
   *
   * <p>Kotlin: {@code suspend fun E7VM.callSuspend(m: E7VM.Module, f: String, vararg a: Any?): Any? =
   * callAsync(vmThread, m, f, *a).await()} (kotlinx-coroutines' {@code CompletionStage.await}).
   */
  @SuppressWarnings("NewApi")  // CompletableFuture: Android API 24+ (the blocking callAwait works on any level)
  public CompletableFuture<Object> callAsync(Executor vmThread, Module module, String function, Object... args) {
    return toFuture(new AsyncCall<Object>(vmThread), h -> nativeCall(h, module.ptr, function, args));
  }

  /**
   * Calls an exported function and blocks until it finishes: the calling thread is the VM's thread and
   * settles the async host functions' results in the order they complete. Interrupting the thread abandons
   * the call. Throws E7Exception like {@link #call}.
   */
  public Object callAwait(Module module, String function, Object... args) throws InterruptedException {
    return this.<Object>await(h -> nativeCall(h, module.ptr, function, args));
  }

  /** {@link #load} for a module whose top-level code awaits async host functions; see {@link #callAsync}. */
  @SuppressWarnings("NewApi")
  public CompletableFuture<Module> loadAsync(Executor vmThread, byte[] bytecode) {
    return toFuture(new AsyncCall<Module>(vmThread), h -> loadStep(h, bytecode));
  }

  /** {@link #load} for a module whose top-level code awaits async host functions; see {@link #callAwait}. */
  public Module loadAwait(byte[] bytecode) throws InterruptedException {
    return this.<Module>await(h -> loadStep(h, bytecode));
  }

  /** Number of async host operations the VM waits for (e7_open_tickets). */
  public long openTickets() { return nativeOpenTickets(check()); }

  // runs in AsyncCall.step, so `current` is the load's call
  private Object loadStep(long h, byte[] bytecode) {
    try {
      return new Module(nativeLoad(h, bytecode));
    } catch (E7Exception e) {
      if (e.status == E7Exception.PENDING) current.module = new Module(nativePendingModule(h));
      throw e;
    }
  }

  private interface Step {
    Object run(long handle);
  }

  @SuppressWarnings("NewApi")
  private static <T> CompletableFuture<T> toFuture(AsyncCall<T> c, Step first) {
    CompletableFuture<T> f = new CompletableFuture<>();
    c.onDone = () -> {
      if (c.error == null) f.complete(c.result);
      else f.completeExceptionally(c.error);
    };
    f.whenComplete((r, t) -> {
      if (f.isCancelled()) {
        c.cancelled = true;
        c.post(() -> c.abandon(null));
      }
    });
    c.post(() -> c.start(first));
    return f;
  }

  // The blocking variant: the calling thread runs the call and the completions (the AsyncCall's executor
  // is a queue it drains). Uses no CompletableFuture, so it works on every Android API level.
  private <T> T await(Step first) throws InterruptedException {
    LinkedBlockingQueue<Runnable> queue = new LinkedBlockingQueue<>();
    AsyncCall<T> c = new AsyncCall<>(queue::add);
    c.onDone = () -> queue.add(() -> {});  // wakes the loop (close() reports from another thread)
    c.start(first);
    try {
      while (!c.isDone()) queue.take().run();
    } catch (InterruptedException e) {
      c.abandon(new CancellationException("interrupted"));
      throw e;
    }
    Throwable t = c.error;
    if (t == null) return c.result;
    if (t instanceof RuntimeException) throw (RuntimeException) t;
    if (t instanceof Error) throw (Error) t;
    throw new E7Exception(E7Exception.RUNTIME, String.valueOf(t));
  }

  @SuppressWarnings("NewApi")  // only reached through async natives (CompletionStage: API 24+)
  static String messageOf(Throwable t) {
    while ((t instanceof CompletionException || t instanceof ExecutionException) && t.getCause() != null) t = t.getCause();
    return t.getMessage() != null ? t.getMessage() : t.toString();
  }

  /** One async call or load: runs its steps on the VM's thread and settles the tickets of its host operations. */
  private final class AsyncCall<T> {
    final Executor vmThread;
    Module module;              // loadAsync / loadAwait: the result once the initializer has finished
    Runnable onDone;            // callAsync / loadAsync: completes the future
    volatile boolean cancelled; // the future was cancelled
    T result;
    Throwable error;
    private boolean done;       // VM thread only: finished or abandoned (its tickets are gone)
    private volatile boolean reported;

    AsyncCall(Executor vmThread) { this.vmThread = vmThread; }

    boolean isDone() { return reported; }

    void post(Runnable r) {
      try {
        vmThread.execute(r);
      } catch (RuntimeException e) {  // e.g. RejectedExecutionException after a shutdown
        report(null, e);
      }
    }

    void start(Step first) {
      if (cancelled) return;
      if (current != null) {
        done = true;
        report(null, new E7Exception(E7Exception.STATE, "VM waits for host operations of another call; finish or abandon it first"));
        return;
      }
      step(first);
    }

    // runs the call, the load or a resume; on E7_PENDING it waits for the next completion
    @SuppressWarnings("unchecked")
    private void step(Step s) {
      long h = handle;
      if (h == 0) {
        finish(null, new IllegalStateException("VM is closed"));
        return;
      }
      current = this;
      Object r;
      try {
        r = s.run(h);
      } catch (E7Exception e) {
        if (e.status == E7Exception.PENDING) return;
        if (e.status == E7Exception.SUSPENDED) {  // an async call does not stop for the budget: abandon it
          nativeAbortSuspended(h);
          e = new E7Exception(E7Exception.SUSPENDED, "instruction budget exhausted (the async call was abandoned)");
        }
        finish(null, e);
        return;
      } catch (RuntimeException e) {
        finish(null, e);
        return;
      }
      finish((T) (module != null ? module : r), null);
    }

    // a host operation of this call completed (VM thread): settle its ticket, then let the script continue
    void completed(String name, long ticket, Object value, Throwable error) {
      if (done) return;
      long h = handle;
      if (h == 0) {
        finish(null, new IllegalStateException("VM is closed"));
        return;
      }
      if (settle(h, name, ticket, value, error)) step(E7VM::nativeResume);
    }

    void finish(T r, Throwable error) {
      done = true;
      if (current == this) current = null;
      report(r, error);
    }

    // VM thread: cancelled, interrupted or abortSuspended()
    void abandon(Throwable reason) {
      if (done) return;
      done = true;
      if (current == this) {
        current = null;
        if (handle != 0) nativeAbortSuspended(handle);
      }
      report(null, reason != null ? reason : new CancellationException("cancelled"));
    }

    // the outcome, once (close() may report from another thread)
    void report(T r, Throwable error) {
      synchronized (this) {
        if (reported) return;
        result = r;
        this.error = error;
        reported = true;
      }
      if (onDone != null) onDone.run();
    }
  }

  // Settles a ticket on the VM's thread; false if it is not open (its call was abandoned). A value of the
  // wrong type rejects the promise instead, so the script does not wait forever.
  private static boolean settle(long h, String name, long ticket, Object value, Throwable error) {
    int st;
    if (error == null) {
      st = nativeSettle(h, ticket, value);
      if (st == E7Exception.ARGS) st = nativeSettleError(h, ticket, name + ": " + nativeLastError(h));
    } else {
      st = nativeSettleError(h, ticket, messageOf(error));
    }
    return st == 0;
  }

  /** Called by the JNI layer when a script calls an async native (on the VM's thread). */
  @SuppressWarnings("NewApi")  // CompletionStage: Android API 24+
  static final class AsyncBridge {
    final E7VM vm;
    final String name;
    final AsyncNativeFunction fn;

    AsyncBridge(E7VM vm, String name, AsyncNativeFunction fn) {
      this.vm = vm;
      this.name = name;
      this.fn = fn;
    }

    // null: started (the ticket is settled at once or later); otherwise the error the script's call throws
    String start(Object[] args, long ticket) {
      CompletionStage<?> stage;
      try {
        stage = fn.invoke(args);
      } catch (Throwable t) {
        return t.toString();  // as for a NativeFunction
      }
      if (stage == null) return name + " returned null instead of a CompletionStage";
      AsyncCall<?> call = vm.current;
      Thread vmThread = Thread.currentThread();
      AtomicBoolean registering = new AtomicBoolean(true);
      stage.whenComplete((v, t) -> {
        if (registering.get() && Thread.currentThread() == vmThread) {
          long h = vm.handle;
          if (h != 0) settle(h, name, ticket, v, t);  // already complete: settled before the script goes on
        } else if (call != null) {
          call.post(() -> call.completed(name, ticket, v, t));
        }  // else a synchronous call(), which abandons the call when it returns E7_PENDING
      });
      registering.set(false);
      return null;
    }
  }

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

  /** Library version, e.g. "0.1.0". */
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
      AsyncCall<?> c = current;
      current = null;
      nativeFree(handle);
      handle = 0;
      if (c != null) c.report(null, new IllegalStateException("VM is closed"));
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
  private static native void nativeRegisterAsync(long vm, String name, String sig, AsyncBridge bridge);
  private static native int nativeSettle(long vm, long ticket, Object value);
  private static native int nativeSettleError(long vm, long ticket, String message);
  private static native long nativeOpenTickets(long vm);
  private static native long nativePendingModule(long vm);
  private static native String nativeLastError(long vm);
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
