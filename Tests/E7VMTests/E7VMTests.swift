import CE7VM
import E7VM
import Foundation
import XCTest

private let signSource = """
  import { hmac } from "e7:crypto";
  import { hexEncode } from "e7:encoding";
  export function sign(message: string, secret: string): string {
    const enc = new TextEncoder();
    return hexEncode(hmac("sha256", enc.encode(secret), enc.encode(message)));
  }
  """
private let expectedMac = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6"

private struct HostError: Error, CustomStringConvertible {
  let text: String
  var description: String { text }
}

private func status(_ s: e7_status) -> Int32 { Int32(s.rawValue) }

/// Runs `body`, expecting it to throw an E7Error, and returns that error.
@discardableResult
private func expectE7Error<T>(_ body: @autoclosure () throws -> T, file: StaticString = #filePath, line: UInt = #line)
  -> E7Error?
{
  do {
    _ = try body()
    XCTFail("expected E7Error", file: file, line: line)
    return nil
  } catch let e as E7Error {
    return e
  } catch {
    XCTFail("unexpected error \(error)", file: file, line: line)
    return nil
  }
}

private func seed(_ base: UInt8) -> Data { Data((0..<32).map { base &+ UInt8($0) }) }

final class E7VMTests: XCTestCase {
  func testVersion() {
    // "major.minor.patch"; the exact value is checked against CMakeLists.txt by scripts/check-versions.py
    XCTAssertNotNil(E7VM.version.range(of: #"^\d+\.\d+\.\d+$"#, options: .regularExpression), E7VM.version)
  }

  func testCompileLoadCallString() throws {
    let code = try E7VM.compile(signSource, fileName: "sign.ts")
    XCTAssertFalse(code.isEmpty)
    let vm = try E7VM(memoryLimit: 32 << 20)
    let m = try vm.load(code)
    XCTAssertTrue(vm.hasFunction(m, "sign"))
    XCTAssertFalse(vm.hasFunction(m, "nope"))
    vm.setBudget(10_000_000)
    XCTAssertEqual(try vm.call(m, "sign", .string("amount=100&currency=CNY"), .string("s3cr3t")), .string(expectedMac))
    XCTAssertEqual(expectE7Error(try vm.call(m, "nope"))?.status, status(E7_ERR_NOT_FOUND))
    XCTAssertEqual(expectE7Error(try vm.call(m, "sign", .number(1), .string("x")))?.status, status(E7_ERR_ARGS))
  }

  func testExampleSignTs() throws {
    // examples/embed/sign.ts, the quick-start module
    let url = URL(fileURLWithPath: #filePath)
      .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
      .appendingPathComponent("examples/embed/sign.ts")
    guard let source = try? String(contentsOf: url, encoding: .utf8) else {
      throw XCTSkip("examples/embed/sign.ts not available")
    }
    let vm = try E7VM()
    let m = try vm.loadSource(source, fileName: "sign.ts")
    XCTAssertEqual(try vm.call(m, "main"), .string(expectedMac))
    XCTAssertEqual(try vm.call(m, "sign", .string("amount=100&currency=CNY"), .string("s3cr3t")), .string(expectedMac))
  }

  func testCompileError() {
    let e = expectE7Error(try E7VM.compile("let x: number = 'a';", fileName: "bad.ts"))
    XCTAssertEqual(e?.status, status(E7_ERR_COMPILE))
    XCTAssertTrue(e?.message.contains("bad.ts:1") ?? false, e?.message ?? "")
    let vm = try? E7VM()
    XCTAssertEqual(expectE7Error(try vm!.loadSource("export function f(): number { return 'x'; }"))?.status,
                   status(E7_ERR_COMPILE))
  }

  func testValueRoundTrip() throws {
    let vm = try E7VM()
    let m = try vm.loadSource("""
      export function add(a: number, b: number): number { return a + b; }
      export function neg(x: i64): i64 { return -x; }
      export function i32id(x: i32): i32 { return x; }
      export function big(x: u64): u64 { return x - 1; }
      export function u8inc(x: u8): u8 { return x + 1; }
      export function flip(b: boolean): boolean { return !b; }
      export function rev(b: Uint8Array): Uint8Array { return b.slice().reverse(); }
      export function len(b: Uint8Array): number { return b.length; }
      export function greet(n: string): string { return `héllo ${n} 😀`; }
      export function nothing(): void {}
      """, fileName: "values.ts")
    XCTAssertEqual(try vm.call(m, "add", .number(1.5), .number(2.25)), .number(3.75))
    XCTAssertEqual(try vm.call(m, "neg", .int(Int64.max)), .int(-Int64.max))
    XCTAssertEqual(try vm.call(m, "i32id", .int(-123_456)), .int(-123_456))
    XCTAssertEqual(try vm.call(m, "big", .uint(0)), .uint(UInt64.max))
    XCTAssertEqual(try vm.call(m, "big", .uint(UInt64.max)), .uint(UInt64.max - 1))
    XCTAssertEqual(try vm.call(m, "u8inc", .uint(255)), .uint(0))
    XCTAssertEqual(try vm.call(m, "flip", .bool(true)), .bool(false))
    XCTAssertEqual(try vm.call(m, "flip", .bool(false)), .bool(true))
    XCTAssertEqual(try vm.call(m, "rev", .bytes(Data([1, 2, 3]))), .bytes(Data([3, 2, 1])))
    XCTAssertEqual(try vm.call(m, "rev", .bytes(Data())), .bytes(Data()))
    XCTAssertEqual(try vm.call(m, "len", .bytes(Data(repeating: 7, count: 1000))), .number(1000))
    XCTAssertEqual(try vm.call(m, "greet", .string("Zoë")), .string("héllo Zoë 😀"))
    XCTAssertEqual(try vm.call(m, "greet", .string("")), .string("héllo  😀"))
    XCTAssertEqual(try vm.call(m, "nothing"), .void)
    XCTAssertEqual(expectE7Error(try vm.call(m, "add", .string("x"), .number(1)))?.status, status(E7_ERR_ARGS))
    XCTAssertEqual(expectE7Error(try vm.call(m, "add", .number(1)))?.status, status(E7_ERR_ARGS))
    XCTAssertGreaterThan(vm.memoryUsed, 0)
    vm.collectGarbage()
  }

  func testNativesPrintAndKeyProvider() throws {
    let vm = try E7VM()
    var printed = [String]()
    vm.printHandler = { printed.append($0) }
    try vm.registerNative("hostAdd", signature: "nn>n") { a in
      guard case .number(let x) = a[0], case .number(let y) = a[1] else { return .void }
      return .number(x + y)
    }
    try vm.registerNative("hostTag", signature: "s>s") { a in
      guard case .string(let s) = a[0] else { return .void }
      return .string("<" + s + ">")
    }
    try vm.registerNative("hostInt", signature: "7>7") { a in
      guard case .int(let x) = a[0] else { return .void }
      return .int(x * 2)
    }
    try vm.registerNative("hostCount", signature: ">5") { _ in .int(41) }
    try vm.registerNative("hostBytes", signature: "y>y") { a in
      guard case .bytes(let d) = a[0] else { return .void }
      return .bytes(d + Data([0xFF]))
    }
    try vm.registerNative("hostFail", signature: "s>v") { a in
      guard case .string(let s) = a[0] else { return .void }
      throw HostError(text: "host says " + s)
    }
    vm.keyProvider = { id in id == 7 ? Data("s3cr3t".utf8) : nil }
    let m = try vm.loadSource("""
      import { hmac } from "e7:crypto";
      import type { KeyHandle } from "e7:crypto";
      import { hexEncode } from "e7:encoding";
      declare function hostAdd(a: number, b: number): number;
      declare function hostTag(s: string): string;
      declare function hostInt(x: i64): i64;
      declare function hostCount(): i32;
      declare function hostBytes(b: Uint8Array): Uint8Array;
      declare function hostFail(s: string): void;
      console.log("loaded " + hostTag("é✓"));
      export function add(a: number, b: number): number { return hostAdd(a, b); }
      export function twice(x: i64): i64 { return hostInt(x); }
      export function count(): i32 { return hostCount() + 1; }
      export function bytes(b: Uint8Array): number { return hostBytes(b).length; }
      export function mac(k: KeyHandle, m: string): string { return hexEncode(hmac("sha256", k, new TextEncoder().encode(m))); }
      export function fail(): void { hostFail("no"); }
      export function log(s: string): void { console.log(s); }
      """, fileName: "t.ts")
    XCTAssertEqual(printed, ["loaded <é✓>"])
    XCTAssertEqual(try vm.call(m, "add", .number(1.5), .number(2.25)), .number(3.75))
    XCTAssertEqual(try vm.call(m, "twice", .int(-21)), .int(-42))
    XCTAssertEqual(try vm.call(m, "count"), .int(42))
    XCTAssertEqual(try vm.call(m, "bytes", .bytes(Data([1, 2]))), .number(3))
    for _ in 0..<3 {  // repeated key resolution reuses/zeroes the key buffer
      XCTAssertEqual(try vm.call(m, "mac", .handle(KeyHandle(7)), .string("amount=100&currency=CNY")), .string(expectedMac))
    }
    XCTAssertEqual(expectE7Error(try vm.call(m, "mac", .handle(KeyHandle(8)), .string("x")))?.status, status(E7_ERR_RUNTIME))
    let e = expectE7Error(try vm.call(m, "fail"))
    XCTAssertEqual(e?.status, status(E7_ERR_RUNTIME))
    XCTAssertTrue(e?.message.contains("host says no") ?? false, e?.message ?? "")
    try vm.call(m, "log", .string("second"))
    XCTAssertEqual(printed, ["loaded <é✓>", "second"])
    vm.printHandler = nil
    try vm.call(m, "log", .string("dropped"))
    XCTAssertEqual(printed.count, 2)
    vm.keyProvider = nil
    XCTAssertEqual(expectE7Error(try vm.call(m, "mac", .handle(KeyHandle(7)), .string("x")))?.status, status(E7_ERR_RUNTIME))
  }

  func testUnknownNativeFailsLoad() throws {
    let vm = try E7VM()
    let e = expectE7Error(try vm.loadSource("declare function missing(): number;\nexport function f(): number { return missing(); }"))
    XCTAssertEqual(e?.status, status(E7_ERR_LOAD))
  }

  func testBudgetAndMemoryLimit() throws {
    let vm = try E7VM(memoryLimit: 8 << 20)
    let m = try vm.loadSource("""
      export function spin(): void { while (true) {} }
      export function grow(): number { const a: number[] = []; while (true) a.push(1); return 0; }
      export function ok(): number { return 1; }
      """)
    vm.setBudget(1_000_000)
    XCTAssertEqual(expectE7Error(try vm.call(m, "spin"))?.status, status(E7_ERR_BUDGET))
    vm.setBudget(0)
    XCTAssertEqual(expectE7Error(try vm.call(m, "grow"))?.status, status(E7_ERR_MEMORY))
    // the VM stays usable after a limit error
    XCTAssertEqual(try vm.call(m, "ok"), .number(1))
  }

  func testSuspendResume() throws {
    let vm = try E7VM(suspendOnBudget: true)
    let m = try vm.loadSource("export function sum(n: number): number { let s = 0; for (let i = 0; i < n; i++) s += i; return s; }")
    vm.setBudget(100_000)
    var slices = 0
    var result: E7Value?
    do {
      result = try vm.call(m, "sum", .number(1_000_000))
    } catch let e as E7Error where e.isSuspended {
      while result == nil {
        slices += 1
        do { result = try vm.resume() } catch let e2 as E7Error where e2.isSuspended {}
      }
    }
    XCTAssertGreaterThan(slices, 1)
    XCTAssertEqual(result, .number(499_999_500_000))

    // abandon a suspended call; the VM accepts new calls afterwards
    vm.setBudget(1_000)
    XCTAssertTrue(expectE7Error(try vm.call(m, "sum", .number(1_000_000)))?.isSuspended ?? false)
    XCTAssertEqual(expectE7Error(try vm.call(m, "sum", .number(1)))?.status, status(E7_ERR_STATE))
    vm.abortSuspended()
    vm.setBudget(0)
    XCTAssertEqual(try vm.call(m, "sum", .number(10)), .number(45))
  }

  func testSigning() throws {
    let code = try E7VM.compile(signSource, fileName: "sign.ts")
    let key = seed(1)
    let pub = try E7VM.publicKey(seed: key)
    XCTAssertEqual(pub.count, 32)
    XCTAssertEqual(pub, try E7VM.publicKey(seed: key))
    let signed = try E7VM.sign(code, seed: key)
    XCTAssertGreaterThan(signed.count, code.count)
    let args: [E7Value] = [.string("amount=100&currency=CNY"), .string("s3cr3t")]

    // signed module loads without trusted keys
    let open = try E7VM()
    XCTAssertEqual(try open.call(open.load(signed), "sign", args[0], args[1]), .string(expectedMac))

    // trusted key: accepts signed, rejects unsigned
    let strict = try E7VM(trustedKeys: [pub])
    XCTAssertEqual(try strict.call(strict.load(signed), "sign", args[0], args[1]), .string(expectedMac))
    XCTAssertEqual(expectE7Error(try strict.load(code))?.status, status(E7_ERR_SIGNATURE))
    // source compiled by the host itself is trusted (e7_load_source skips the signature check)
    XCTAssertNoThrow(try strict.loadSource(signSource))

    // several trusted keys
    let multi = try E7VM(trustedKeys: [try E7VM.publicKey(seed: seed(100)), pub])
    XCTAssertNoThrow(try multi.load(signed))

    // a module signed by another key is rejected
    let other = try E7VM(trustedKeys: [try E7VM.publicKey(seed: seed(50))])
    XCTAssertEqual(expectE7Error(try other.load(signed))?.status, status(E7_ERR_SIGNATURE))

    // tampered signed module is rejected
    var tampered = signed
    tampered[tampered.count / 3] ^= 0x40
    XCTAssertNotNil(expectE7Error(try strict.load(tampered)))

    // argument validation
    XCTAssertEqual(expectE7Error(try E7VM(trustedKeys: [Data([1, 2, 3])]))?.status, status(E7_ERR_ARGS))
    XCTAssertEqual(expectE7Error(try E7VM.sign(code, seed: Data([1])))?.status, status(E7_ERR_ARGS))
    XCTAssertEqual(expectE7Error(try E7VM.publicKey(seed: Data()))?.status, status(E7_ERR_ARGS))
  }

  func testTamperedBytecodeRejected() throws {
    var code = try E7VM.compile(signSource, fileName: "sign.ts")
    code[code.count / 2] ^= 0x40
    let vm = try E7VM()
    let st = expectE7Error(try vm.load(code))?.status
    XCTAssertTrue(st == status(E7_ERR_LOAD) || st == status(E7_ERR_VERIFY), "status \(String(describing: st))")
    XCTAssertNotNil(expectE7Error(try vm.load(Data())))
  }

  func testVersionNumberAndInspect() throws {
    let vn = E7VM.versionNumber
    XCTAssertEqual(E7VM.version, "\(vn >> 16).\((vn >> 8) & 0xFF).\(vn & 0xFF)")
    let code = try E7VM.compile(signSource, fileName: "sign.ts")
    var info = E7VM.inspect(try E7VM.sign(code, seed: seed(1)))
    XCTAssertTrue(info.canRun)
    XCTAssertEqual(info.format, 2)
    XCTAssertEqual(info.requiresVersionString, "0.2.0")
    XCTAssertTrue(info.isSigned)
    XCTAssertEqual(info.keyId.count, 8)
    XCTAssertNotEqual(info.keyId, Data(count: 8))
    XCTAssertFalse(info.hasDebugInfo)
    XCTAssertFalse(E7VM.inspect(code).isSigned)
    let newer = withRequires(code, 0x63_0000)
    info = E7VM.inspect(newer)
    XCTAssertEqual(info.status, status(E7_ERR_VERSION))
    XCTAssertEqual(info.requiresVersionString, "99.0.0")
    XCTAssertEqual(expectE7Error(try E7VM().load(newer))?.status, status(E7_ERR_VERSION))
    XCTAssertEqual(E7VM.inspect(Data([1, 2, 3])).status, status(E7_ERR_LOAD))
    XCTAssertEqual(E7VM.inspect(Data()).status, status(E7_ERR_LOAD))
  }

  func testGcStatsAndGrowth() throws {
    let vm = try E7VM(gcGrowthPercent: 150)
    let m = try vm.loadSource(
      "let keep: number[][] = [];\nexport function churn(): number { for (let i = 0; i < 20000; i++) { keep.push([i]); if (keep.length > 500) keep = []; } return keep.length; }"
    )
    try vm.call(m, "churn")
    vm.collectGarbage()
    let gs = vm.gcStats()
    XCTAssertGreaterThanOrEqual(gs.collections, 1)
    XCTAssertGreaterThan(gs.maxPauseNanoseconds, 0)
    XCTAssertGreaterThanOrEqual(gs.totalPauseNanoseconds, gs.maxPauseNanoseconds)
    XCTAssertEqual(gs.heapBytes, vm.memoryUsed)
    XCTAssertGreaterThanOrEqual(gs.peakHeapBytes, gs.heapBytes)
  }

  func testDetachedSignatures() throws {
    let manifest = Data("{\"version\":\"2\",\"killSwitch\":false}".utf8)
    let sig = try E7VM.signDetached(manifest, seed: seed(1))
    XCTAssertEqual(
      sig.map { String(format: "%02x", $0) }.joined(),
      "1549a30db54e6fc64e15efee8a9b5222a22be751830dd98dd5f97d30c1bc6b09"
        + "2cdaa2efdc5a609a338b827ca32a0a9d1d9212ffd3eb51bfc9eb3b073487e607")
    let pub = try E7VM.publicKey(seed: seed(1)), other = try E7VM.publicKey(seed: seed(100))
    XCTAssertTrue(E7VM.verifyDetached(manifest, signature: sig, trustedKeys: [pub]))
    XCTAssertTrue(E7VM.verifyDetached(manifest, signature: sig, trustedKeys: [other, pub]))
    XCTAssertFalse(E7VM.verifyDetached(manifest, signature: sig, trustedKeys: [other]))
    XCTAssertFalse(E7VM.verifyDetached(manifest, signature: sig, trustedKeys: []))
    XCTAssertFalse(E7VM.verifyDetached(Data("x".utf8), signature: sig, trustedKeys: [pub]))
    XCTAssertTrue(E7VM.verifyDetached(Data(), signature: try E7VM.signDetached(Data(), seed: seed(1)), trustedKeys: [pub]))
    XCTAssertEqual(expectE7Error(try E7VM.signDetached(manifest, seed: Data([1])))?.status, status(E7_ERR_ARGS))
  }

  func testSeparateVMsAreIndependent() throws {
    let code = try E7VM.compile("let n = 0;\nexport function next(): number { n++; return n; }")
    let a = try E7VM(), b = try E7VM()
    let ma = try a.load(code), mb = try b.load(code)
    XCTAssertEqual(try a.call(ma, "next"), .number(1))
    XCTAssertEqual(try a.call(ma, "next"), .number(2))
    XCTAssertEqual(try b.call(mb, "next"), .number(1))
  }
}
