package org.e7vm;

import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Async host functions (registerAsyncNative, callAsync, callAwait, loadAwait) without a test framework (each
 * scenario throws AssertionError on failure). Run by the JUnit test E7VMAsyncTest (Gradle :jvm:test and the
 * Android instrumented tests) and by platform/java/test/AsyncTest.java (plain javac + java, CI job "jni").
 */
public final class AsyncScenarios {
  public static final String[] ALL = {
    "settledLaterFromAnotherThread", "settledAtOnce", "rejectedAndCaught", "promiseAllReverseOrder",
    "pendingCallBlocksSecondCall", "abandonPendingCall", "syncCallOfPendingExport", "topLevelAwait",
  };

  public static void run(String name) throws Exception {
    switch (name) {
      case "settledLaterFromAnotherThread": settledLaterFromAnotherThread(); break;
      case "settledAtOnce": settledAtOnce(); break;
      case "rejectedAndCaught": rejectedAndCaught(); break;
      case "promiseAllReverseOrder": promiseAllReverseOrder(); break;
      case "pendingCallBlocksSecondCall": pendingCallBlocksSecondCall(); break;
      case "abandonPendingCall": abandonPendingCall(); break;
      case "syncCallOfPendingExport": syncCallOfPendingExport(); break;
      case "topLevelAwait": topLevelAwait(); break;
      default: throw new IllegalArgumentException(name);
    }
  }

  static final String APP = String.join("\n",
      "declare function fetchKey(id: string): Promise<Uint8Array>;",
      "declare function now(): Promise<number>;",
      "export async function keyLength(id: string): Promise<number> {",
      "  const k = await fetchKey(id);",
      "  return k.length + (await now());",
      "}",
      "export async function both(): Promise<string> {",
      "  const order: string[] = [];",
      "  const pa = fetchKey('a').then((k) => { order.push('a'); return k; });",
      "  const pb = fetchKey('b').then((k) => { order.push('b'); return k; });",
      "  const [a, b] = await Promise.all([pa, pb]);",
      "  return `${a.length},${b.length},${order.join('')}`;",
      "}",
      "export async function failing(): Promise<number> {",
      "  try {",
      "    await fetchKey('x');",
      "    return 1;",
      "  } catch (e) {",
      "    if (e instanceof Error) console.log('caught ' + e.message);",
      "    return -1;",
      "  }",
      "}",
      "export async function stamp(): Promise<number> { return (await now()) * 2; }",
      "export function syncCall(): number { return 3; }");

  /** One call of the async native fetchKey: the test completes its future. */
  static final class Op {
    final String key;
    final CompletableFuture<Object> future = new CompletableFuture<>();
    Op(String key) { this.key = key; }
  }

  /** A VM on its own thread (a single-thread executor) with the async natives fetchKey (completed by the test) and now (at once). */
  static final class Fixture implements AutoCloseable {
    final ExecutorService vmThread = Executors.newSingleThreadExecutor();
    final LinkedBlockingQueue<Op> started = new LinkedBlockingQueue<>();
    final StringBuilder printed = new StringBuilder();
    final E7VM vm = new E7VM();
    final E7VM.Module m;

    Fixture() {
      vm.setPrintHandler(printed::append);
      vm.registerAsyncNative("fetchKey", "s>y", args -> {
        Op op = new Op((String) args[0]);
        started.add(op);
        return op.future;
      });
      vm.registerAsyncNative("now", ">n", args -> CompletableFuture.completedFuture(5.0));
      m = vm.loadSource(APP, "app.ts");
    }

    Op next() throws InterruptedException {
      Op op = started.poll(10, TimeUnit.SECONDS);
      check(op != null, "the async native was called");
      return op;
    }

    /** Runs on the VM's thread and returns the result or the exception thrown. */
    Object onVm(java.util.concurrent.Callable<Object> f) throws Exception {
      try {
        return vmThread.submit(f).get(10, TimeUnit.SECONDS);
      } catch (ExecutionException e) {
        return e.getCause();
      }
    }

    @Override
    public void close() throws Exception {
      vmThread.submit(vm::close).get(10, TimeUnit.SECONDS);
      vmThread.shutdown();
    }
  }

  // ------------------------------------------------------------------ scenarios
  static void settledLaterFromAnotherThread() throws Exception {
    try (Fixture f = new Fixture()) {
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k1");
      Op op = f.next();
      check("k1".equals(op.key) && !r.isDone(), "keyLength waits for fetchKey('k1')");
      check((Long) f.onVm(f.vm::openTickets) == 1, "one open ticket");
      completeLater(op, new byte[] {1, 2, 3, 4});
      check((Double) r.get(10, TimeUnit.SECONDS) == 9.0, "keyLength = 4 + 5");
      check((Long) f.onVm(f.vm::openTickets) == 0, "no open ticket");
    }
    // callAwait: the calling thread is the VM's thread
    try (E7VM vm = new E7VM()) {
      vm.registerAsyncNative("fetchKey", "s>y", args -> CompletableFuture.supplyAsync(() -> {
        sleep(20);
        return new byte[((String) args[0]).length()];
      }));
      vm.registerAsyncNative("now", ">n", args -> CompletableFuture.supplyAsync(() -> 5.0));
      E7VM.Module m = vm.loadSource(APP, "app.ts");
      check((Double) vm.callAwait(m, "keyLength", "abc") == 8.0, "callAwait keyLength = 3 + 5");
      String both = (String) vm.callAwait(m, "both");  // settled in the order the operations complete
      check("1,1,ab".equals(both) || "1,1,ba".equals(both), "callAwait both: " + both);
    }
  }

  static void settledAtOnce() throws Exception {
    try (Fixture f = new Fixture()) {
      // now() completes at once: no ticket stays open, so even the synchronous call works
      check((Double) f.onVm(() -> f.vm.call(f.m, "stamp")) == 10.0, "call of an async export settled at once");
      check((Double) f.vm.callAsync(f.vmThread, f.m, "stamp").get(10, TimeUnit.SECONDS) == 10.0, "callAsync stamp");
      check((Double) f.onVm(() -> f.vm.callAwait(f.m, "stamp")) == 10.0, "callAwait stamp");
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k");
      Op op = f.next();
      op.future.complete(new byte[2]);  // completed on this (test) thread
      check((Double) r.get(10, TimeUnit.SECONDS) == 7.0, "keyLength = 2 + 5");
    }
  }

  static void rejectedAndCaught() throws Exception {
    try (Fixture f = new Fixture()) {
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "failing");
      Op op = f.next();
      new Thread(() -> op.future.completeExceptionally(new IllegalStateException("no such key"))).start();
      check((Double) r.get(10, TimeUnit.SECONDS) == -1.0, "failing returns -1");
      check("caught no such key".equals(f.onVm(f.printed::toString)), "the script caught the rejection: " + f.printed);
      // an uncaught rejection fails the call
      r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "gone");
      f.next().future.completeExceptionally(new RuntimeException("gone"));
      E7Exception e = failure(r);
      check(e.status == E7Exception.RUNTIME && e.getMessage().contains("gone"), "uncaught rejection: " + e.getMessage());
      // a value of the wrong type rejects the promise
      r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "bad");
      f.next().future.complete("not bytes");
      e = failure(r);
      check(e.status == E7Exception.RUNTIME && e.getMessage().contains("fetchKey"), "wrong value type: " + e.getMessage());
    }
  }

  static void promiseAllReverseOrder() throws Exception {
    try (Fixture f = new Fixture()) {
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "both");
      Op a = f.next(), b = f.next();
      check("a".equals(a.key) && "b".equals(b.key), "both started fetchKey('a') and fetchKey('b')");
      Thread tb = new Thread(() -> b.future.complete(new byte[4]));
      tb.start();
      tb.join();
      sleep(20);
      check(!r.isDone(), "still waiting for 'a'");
      new Thread(() -> a.future.complete(new byte[1])).start();
      check("1,4,ba".equals(r.get(10, TimeUnit.SECONDS)), "Promise.all keeps the order; settled b then a");
    }
  }

  static void pendingCallBlocksSecondCall() throws Exception {
    try (Fixture f = new Fixture()) {
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k1");
      Op op = f.next();
      Object e = f.onVm(() -> f.vm.call(f.m, "syncCall"));
      check(e instanceof E7Exception && ((E7Exception) e).status == E7Exception.STATE, "call while pending: STATE " + e);
      E7Exception e2 = failure(f.vm.callAsync(f.vmThread, f.m, "syncCall"));
      check(e2.status == E7Exception.STATE, "callAsync while pending: STATE");
      op.future.complete(new byte[1]);
      check((Double) r.get(10, TimeUnit.SECONDS) == 6.0, "the first call finishes");
      check((Double) f.onVm(() -> f.vm.call(f.m, "syncCall")) == 3.0, "then calls work again");
    }
  }

  static void abandonPendingCall() throws Exception {
    try (Fixture f = new Fixture()) {
      CompletableFuture<Object> r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k1");
      Op op = f.next();
      check(r.cancel(true), "cancel");
      check((Double) f.onVm(() -> f.vm.call(f.m, "syncCall")) == 3.0, "the VM is free after cancel");
      check((Long) f.onVm(f.vm::openTickets) == 0, "its ticket is gone");
      op.future.complete(new byte[1]);  // a late completion is ignored
      check((Double) f.onVm(() -> f.vm.call(f.m, "syncCall")) == 3.0, "late completion ignored");
      // abortSuspended() on the VM's thread abandons it too
      r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k2");
      op = f.next();
      f.onVm(() -> { f.vm.abortSuspended(); return null; });
      try {
        r.get(10, TimeUnit.SECONDS);
        check(false, "abandoned call must not finish");
      } catch (CancellationException | ExecutionException expected) {
        // the future fails with a CancellationException
      }
      op.future.complete(new byte[1]);  // ignored
      r = f.vm.callAsync(f.vmThread, f.m, "keyLength", "k3");
      Op k3 = f.next();
      check("k3".equals(k3.key), "a new call after abortSuspended");
      k3.future.complete(new byte[3]);
      check((Double) r.get(10, TimeUnit.SECONDS) == 8.0, "the new call finishes");
    }
  }

  static void syncCallOfPendingExport() throws Exception {
    try (Fixture f = new Fixture()) {
      Object e = f.onVm(() -> f.vm.call(f.m, "keyLength", "k1"));
      check(e instanceof E7Exception && ((E7Exception) e).status == E7Exception.PENDING
          && ((E7Exception) e).getMessage().contains("callAsync"), "synchronous call: PENDING " + e);
      f.next().future.complete(new byte[1]);  // ignored: the call was abandoned
      check((Double) f.onVm(() -> f.vm.call(f.m, "syncCall")) == 3.0, "VM usable after the PENDING error");
    }
  }

  static void topLevelAwait() throws Exception {
    String src = String.join("\n",
        "declare function fetchKey(id: string): Promise<Uint8Array>;",
        "console.log('init');",
        "const key = await fetchKey('boot');",
        "export function keySize(): number { return key.length; }");
    try (E7VM vm = new E7VM()) {
      StringBuilder printed = new StringBuilder();
      vm.setPrintHandler(printed::append);
      vm.registerAsyncNative("fetchKey", "s>y", args -> CompletableFuture.supplyAsync(() -> {
        sleep(10);
        return new byte[16];
      }));
      byte[] code = E7VM.compile(src, "boot.ts");
      try {
        vm.load(code);
        check(false, "load must report PENDING");
      } catch (E7Exception e) {
        check(e.status == E7Exception.PENDING, "load: PENDING");
      }
      E7VM.Module m = vm.loadAwait(code);
      check((Double) vm.call(m, "keySize") == 16.0, "loadAwait finished the initializer");
      check("initinit".equals(printed.toString()), "top level ran twice (abandoned, then awaited)");
    }
    ExecutorService vmThread = Executors.newSingleThreadExecutor();
    try (E7VM vm = new E7VM()) {
      vm.setPrintHandler(s -> {});
      vm.registerAsyncNative("fetchKey", "s>y", args -> CompletableFuture.supplyAsync(() -> new byte[8]));
      E7VM.Module m = vm.loadAsync(vmThread, E7VM.compile(src, "boot.ts")).get(10, TimeUnit.SECONDS);
      check((Double) vmThread.submit(() -> vm.call(m, "keySize")).get() == 8.0, "loadAsync");
    } finally {
      vmThread.shutdown();
    }
  }

  // ------------------------------------------------------------------ helpers
  static void check(boolean cond, String what) {
    if (!cond) throw new AssertionError(what);
  }

  static void sleep(long ms) {
    try {
      Thread.sleep(ms);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  static void completeLater(Op op, Object value) {
    new Thread(() -> {
      sleep(30);
      op.future.complete(value);
    }).start();
  }

  static E7Exception failure(CompletableFuture<Object> r) throws Exception {
    try {
      Object v = r.get(10, TimeUnit.SECONDS);
      throw new AssertionError("expected a failure, got " + v);
    } catch (ExecutionException e) {
      check(e.getCause() instanceof E7Exception, "E7Exception expected: " + e.getCause());
      return (E7Exception) e.getCause();
    }
  }

  private AsyncScenarios() {}
}
