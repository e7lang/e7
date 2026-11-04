package org.e7vm;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import org.junit.Test;

/**
 * JUnit port of platform/java/test/E7VMTest.java. Shared by the desktop JVM tests (:jvm:test) and the
 * Android instrumented tests (:library:connectedDebugAndroidTest), so it runs against both the
 * bundled desktop JNI libraries and the AAR's libe7vm_jni.so on an emulator.
 */
public class E7VMApiTest {
  static final String SIGN_HEX = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";

  static final String APP = String.join("\n",
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

  static String resource(String name) throws IOException {
    InputStream in = E7VMApiTest.class.getResourceAsStream("/" + name);
    if (in == null) throw new IOException("missing test resource " + name);
    try {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buf = new byte[8192];
      for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
      return new String(out.toByteArray(), StandardCharsets.UTF_8);
    } finally {
      in.close();
    }
  }

  static byte[] seed() {
    byte[] seed = new byte[32];
    for (int i = 0; i < 32; i++) seed[i] = (byte) (i + 1);
    return seed;
  }

  static E7VM appVm(E7VM.Config cfg, StringBuilder printed) {
    E7VM vm = new E7VM(cfg);
    vm.setPrintHandler(printed::append);
    vm.registerNative("hostTime", ">n", args -> 42.0);
    vm.registerNative("hostTag", "sn>s", args -> args[0] + "#" + ((Double) args[1]).intValue());
    vm.setKeyProvider(id -> id == 1 ? "secret".getBytes(StandardCharsets.UTF_8) : null);
    return vm;
  }

  /** examples/embed/sign.ts: compile, load, call. */
  @Test
  public void signExample() throws Exception {
    byte[] code = E7VM.compile(resource("sign.ts"), "sign.ts");
    try (E7VM vm = new E7VM()) {
      E7VM.Module m = vm.load(code);
      assertTrue(vm.hasFunction(m, "sign"));
      assertFalse(vm.hasFunction(m, "nope"));
      assertEquals(SIGN_HEX, vm.call(m, "sign", "amount=100&currency=CNY", "s3cr3t"));
      assertEquals(SIGN_HEX, vm.call(m, "main"));
    }
  }

  @Test
  public void loadSource() throws Exception {
    try (E7VM vm = new E7VM()) {
      E7VM.Module m = vm.loadSource(resource("sign.ts"), "sign.ts");
      assertEquals(SIGN_HEX, vm.call(m, "main"));
      assertTrue(vm.memoryUsed() > 0);
    }
  }

  @Test
  public void valuesNativesAndPrint() {
    StringBuilder printed = new StringBuilder();
    try (E7VM vm = appVm(new E7VM.Config(), printed)) {
      E7VM.Module m = vm.load(E7VM.compile(APP, "app.ts"));
      assertEquals(42.0, (Double) vm.call(m, "add", 2.0, 40), 0.0);
      assertEquals("héllo Zoë 😀", vm.call(m, "greet", "Zoë"));
      assertEquals("greeting Zoë", printed.toString());
      assertArrayEquals(new byte[] {3, 2, 1}, (byte[]) vm.call(m, "rev", (Object) new byte[] {1, 2, 3}));
      assertEquals(-2L, (long) (Long) vm.call(m, "big", Long.MAX_VALUE));
      assertEquals("t#42", vm.call(m, "host"));
    }
  }

  @Test
  public void keyProviderHmac() throws Exception {
    try (E7VM vm = appVm(new E7VM.Config(), new StringBuilder())) {
      E7VM.Module m = vm.load(E7VM.compile(APP, "app.ts"));
      Mac mac = Mac.getInstance("HmacSHA256");
      mac.init(new SecretKeySpec("secret".getBytes(StandardCharsets.UTF_8), "HmacSHA256"));
      StringBuilder want = new StringBuilder();
      for (byte b : mac.doFinal("payload".getBytes(StandardCharsets.UTF_8))) want.append(String.format("%02x", b & 0xff));
      assertEquals(want.toString(), vm.call(m, "sign", new KeyHandle(1), "payload"));
    }
  }

  @Test
  public void errors() {
    try (E7VM vm = appVm(new E7VM.Config(), new StringBuilder())) {
      E7VM.Module m = vm.load(E7VM.compile(APP, "app.ts"));
      try {
        vm.call(m, "boom");
        fail("exception expected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.RUNTIME, e.status);
        assertTrue(e.getMessage(), e.getMessage().contains("kaboom"));
      }
      try {
        vm.call(m, "add", "x", 1);
        fail("args error expected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.ARGS, e.status);
      }
      try {
        E7VM.compile("export function f(: number {", "bad.ts");
        fail("compile error expected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.COMPILE, e.status);
      }
    }
  }

  @Test
  public void budgetSuspendResume() {
    try (E7VM vm = appVm(new E7VM.Config().suspendOnBudget(true), new StringBuilder())) {
      E7VM.Module m = vm.load(E7VM.compile(APP, "app.ts"));
      vm.setBudget(1000);
      int slices = 0;
      Object res = null;
      try {
        res = vm.call(m, "spin", 100000.0);
        fail("suspension expected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.SUSPENDED, e.status);
      }
      while (res == null) {
        slices++;
        vm.setBudget(1000);
        try {
          res = vm.resume();
        } catch (E7Exception e) {
          assertEquals(E7Exception.SUSPENDED, e.status);
        }
      }
      assertEquals(4999950000.0, (Double) res, 0.0);
      assertTrue("slices=" + slices, slices > 10);
      vm.setBudget(0);
      assertEquals(3.0, (Double) vm.call(m, "add", 1.0, 2.0), 0.0);
    }
  }

  @Test
  public void budgetExhaustedWithoutSuspend() {
    try (E7VM vm = new E7VM()) {
      E7VM.Module m = vm.load(E7VM.compile(APP.replace("declare function hostTime(): number;", "")
          .replace("declare function hostTag(s: string, n: number): string;", "")
          .replace("export function host(): string { return hostTag('t', hostTime()); }", ""), "app.ts"));
      vm.setBudget(1000);
      try {
        vm.call(m, "spin", 1e7);
        fail("budget error expected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.BUDGET, e.status);
      }
    }
  }

  @Test
  public void requiredSignature() {
    byte[] code = E7VM.compile(APP, "app.ts");
    byte[] signed = E7VM.sign(code, seed());
    byte[] pub = E7VM.publicKey(seed());
    assertEquals(32, pub.length);
    try (E7VM vm = appVm(new E7VM.Config().requireSignature(pub), new StringBuilder())) {
      try {
        vm.load(code);
        fail("unsigned module must be rejected");
      } catch (E7Exception e) {
        assertEquals(E7Exception.SIGNATURE, e.status);
      }
      byte[] tampered = signed.clone();
      tampered[tampered.length / 2] ^= 1;
      try {
        vm.load(tampered);
        fail("tampered module must be rejected");
      } catch (E7Exception e) {
        assertTrue("status " + e.status, e.status == E7Exception.SIGNATURE || e.status == E7Exception.LOAD
            || e.status == E7Exception.VERIFY);
      }
      E7VM.Module m = vm.load(signed);
      assertEquals(42.0, (Double) vm.call(m, "add", 2.0, 40), 0.0);
    }
    // Optional signing: without trusted keys, signed modules load too.
    try (E7VM vm = appVm(new E7VM.Config(), new StringBuilder())) {
      assertEquals(5.0, (Double) vm.call(vm.load(signed), "add", 2.0, 3.0), 0.0);
    }
  }

  @Test
  public void closedVm() {
    E7VM vm = new E7VM();
    vm.close();
    vm.close();
    try {
      vm.setBudget(1);
      fail("closed VM must throw");
    } catch (IllegalStateException expected) {
      // ok
    }
  }
}
