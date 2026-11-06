// E7ModuleManager scenarios (same as platform/android/shared-test/org/e7vm/ModuleManagerScenarios.java).
import CE7VM
import CryptoKit
import E7VM
import Foundation
import XCTest

private let base = "https://updates.example.com/app/"
private let manifestURL = URL(string: base + "manifest.json")!
private func seedBytes(_ start: UInt8) -> Data { Data((0..<32).map { start &+ UInt8($0) }) }
private let moduleSeed = seedBytes(1), rotatedSeed = seedBytes(0x41), untrustedSeed = seedBytes(0x81)
private func st(_ s: e7_status) -> Int32 { Int32(s.rawValue) }

private struct Offline: Error, CustomStringConvertible {
  var description: String { "offline" }
}
private struct NotFound: Error, CustomStringConvertible {
  let url: String
  var description: String { "404 \(url)" }
}

/// In-memory update server.
private final class Server {
  var files: [String: Data] = [:]
  var fetched: [String] = []
  var offline = false

  func fetch(_ url: URL) throws -> Data {
    fetched.append(url.absoluteString)
    if offline { throw Offline() }
    guard let d = files[url.absoluteString] else { throw NotFound(url: url.absoluteString) }
    return d
  }

  func publish(_ version: String, _ module: Data, rollout: String, serial: Int, seed: Data, extra: String = "") throws {
    files[base + "app-\(version).e7b"] = module
    try manifest(
      "{\"version\":\"\(version)\",\"url\":\"app-\(version).e7b\",\"sha256\":\"\(sha256Hex(module))\","
        + "\"rolloutPercent\":\(rollout),\"killSwitch\":false,\"serial\":\(serial)\(extra)}", seed: seed)
  }

  func manifest(_ json: String, seed: Data) throws {
    let body = Data(json.utf8)
    files[manifestURL.absoluteString] = body
    files[manifestURL.absoluteString + ".sig"] = Data((hex(try E7VM.signDetached(body, seed: seed)) + "\n").utf8)
  }
}

private func hex(_ d: Data) -> String { d.map { String(format: "%02x", $0) }.joined() }

private func sha256Hex(_ d: Data) -> String { hex(Data(SHA256.hash(data: d))) }

private final class Reports {
  var lines: [String] = []
  var statuses: [Int32] = []
  func add(_ v: String?, _ s: Int32, _ m: String) {
    lines.append("\(v ?? "nil") | \(s) | \(m)")
    statuses.append(s)
  }
  func has(_ v: String?, _ s: e7_status, _ fragment: String) -> Bool {
    lines.contains { $0.hasPrefix("\(v ?? "nil") | \(st(s)) | ") && $0.contains(fragment) }
  }
  func clear() {
    lines = []
    statuses = []
  }
}

private func source(_ answer: Int, selfTestOk: Bool) -> String {
  "export function selfTest(): boolean { return answer() === \(selfTestOk ? answer : -1); }\n"
    + "export function answer(): number { return \(answer); }\n"
}
private func module(_ answer: Int, selfTestOk: Bool = true) throws -> Data {
  try E7VM.sign(try E7VM.compile(source(answer, selfTestOk: selfTestOk), fileName: "app.ts"), seed: moduleSeed)
}
private func answer(_ a: E7ModuleManager.Active?) throws -> Double {
  guard let a = a, case .number(let n) = try a.call("answer") else { return .nan }
  return n
}

// patches the `requires` header field (offset 12) and the checksum (CRC-32 of all but the last 8 bytes)
func withRequires(_ code: Data, _ version: UInt32) -> Data {
  var b = [UInt8](code)
  for i in 0..<4 { b[12 + i] = UInt8((version >> (8 * UInt32(i))) & 0xFF) }
  var c: UInt32 = 0xFFFF_FFFF
  for x in b[0..<(b.count - 8)] {
    c ^= UInt32(x)
    for _ in 0..<8 { c = (c >> 1) ^ (0xEDB8_8320 & (0 &- (c & 1))) }
  }
  c = ~c
  for i in 0..<4 { b[b.count - 4 + i] = UInt8((c >> (8 * UInt32(i))) & 0xFF) }
  return Data(b)
}

private final class Fixture {
  let dir: URL
  let server = Server()
  let reports = Reports()
  var userId = "user-42"
  var setUp: ((E7VM) throws -> Void)?

  init() throws {
    dir = FileManager.default.temporaryDirectory.appendingPathComponent("e7mm-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
  }
  deinit { try? FileManager.default.removeItem(at: dir) }

  func manager() throws -> E7ModuleManager {
    var cfg = E7ModuleManager.Configuration(
      manifestURL: manifestURL,
      trustedKeys: [try E7VM.publicKey(seed: moduleSeed), try E7VM.publicKey(seed: rotatedSeed)],
      storageDirectory: dir, userId: userId,
      fallbackModule: try E7VM.compile("export function answer(): number { return 0; }", fileName: "fallback.ts"),
      fallbackVersion: "bundled")
    cfg.memoryLimit = 16 << 20
    cfg.gcGrowthPercent = 150
    let setUp = self.setUp
    cfg.setUpVM = { vm in
      vm.setBudget(10_000_000)
      try setUp?(vm)
    }
    cfg.reportError = { [reports] v, s, m in reports.add(v, s, m) }
    cfg.maxConsecutiveFailures = 3
    let server = self.server
    return try E7ModuleManager(configuration: cfg, fetcher: { try server.fetch($0) })
  }
}

final class E7ModuleManagerTests: XCTestCase {
  func testHappyPath() throws {
    let f = try Fixture()
    let v2 = try module(2)
    try f.server.publish("2", v2, rollout: "100", serial: 1, seed: moduleSeed)
    let mm = try f.manager()
    var a = try mm.start()
    XCTAssertTrue(a.isFallback)
    XCTAssertEqual(a.version, "bundled")
    XCTAssertEqual(try answer(a), 0)
    a = try mm.update()
    XCTAssertFalse(a.isFallback)
    XCTAssertEqual(a.version, "2")
    XCTAssertEqual(try answer(a), 2)
    XCTAssertEqual(a.sha256, sha256Hex(v2))
    XCTAssertTrue(a === mm.active)
    XCTAssertEqual(f.reports.lines, [])
    f.server.fetched = []
    XCTAssertTrue(try mm.update() === a, "up to date")
    XCTAssertFalse(f.server.fetched.contains(base + "app-2.e7b"), "no second download")
    // restart without network: the last-known-good module comes from disk
    f.server.offline = true
    let mm2 = try f.manager()
    a = try mm2.start()
    XCTAssertFalse(a.isFallback)
    XCTAssertEqual(try answer(a), 2)
    a = try mm2.update()
    XCTAssertEqual(a.version, "2")
    XCTAssertTrue(f.reports.has(nil, E7_ERR_LOAD, "manifest: offline"), "\(f.reports.lines)")
    // key rotation: a manifest signed with the second trusted key
    f.server.offline = false
    try f.server.publish("3", try module(3), rollout: "100", serial: 2, seed: rotatedSeed)
    XCTAssertEqual(try answer(try mm2.update()), 3)
  }

  func testBadSha256() throws {
    let f = try Fixture()
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: moduleSeed)
    f.server.files[base + "app-2.e7b"] = try module(5)  // the CDN serves other bytes
    let a = try f.manager().update()
    XCTAssertTrue(a.isFallback)
    XCTAssertEqual(try answer(a), 0)
    XCTAssertTrue(f.reports.has("2", E7_ERR_SIGNATURE, "sha256 mismatch"), "\(f.reports.lines)")
    let stored = (try? FileManager.default.contentsOfDirectory(atPath: f.dir.appendingPathComponent("modules").path)) ?? []
    XCTAssertEqual(stored, [])
  }

  func testBadManifestSignature() throws {
    let f = try Fixture()
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: untrustedSeed)
    let mm = try f.manager()
    XCTAssertTrue(try mm.update().isFallback, "manifest signed by an untrusted key")
    XCTAssertTrue(f.reports.has(nil, E7_ERR_SIGNATURE, "manifest signature"), "\(f.reports.lines)")
    XCTAssertFalse(f.server.fetched.contains(base + "app-2.e7b"))
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 2)
    // a forged kill switch: tampered body with the old signature, signed by an untrusted key, bad .sig file
    f.reports.clear()
    f.server.files[manifestURL.absoluteString] = Data("{\"killSwitch\":true,\"serial\":9}".utf8)
    XCTAssertEqual(try answer(try mm.update()), 2)
    try f.server.manifest("{\"killSwitch\":true,\"serial\":9}", seed: untrustedSeed)
    XCTAssertEqual(try answer(try mm.update()), 2)
    f.server.files[manifestURL.absoluteString + ".sig"] = Data("not a signature".utf8)
    XCTAssertEqual(try answer(try mm.update()), 2)
    XCTAssertEqual(f.reports.statuses, [st(E7_ERR_SIGNATURE), st(E7_ERR_SIGNATURE), st(E7_ERR_SIGNATURE)])
    XCTAssertFalse(try f.manager().start().isFallback, "the kill switch was not persisted")
  }

  func testKillSwitch() throws {
    let f = try Fixture()
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: moduleSeed)
    let v2Manifest = String(decoding: f.server.files[manifestURL.absoluteString]!, as: UTF8.self)
    let mm = try f.manager()
    XCTAssertEqual(try answer(try mm.update()), 2)
    try f.server.manifest("{\"killSwitch\":true,\"serial\":2}", seed: moduleSeed)
    let a = try mm.update()
    XCTAssertTrue(a.isFallback)
    XCTAssertEqual(try answer(a), 0)
    f.server.offline = true
    XCTAssertTrue(try f.manager().start().isFallback, "kill switch persists across restarts")
    f.server.offline = false
    // replaying the older manifest (serial 1) must not undo the kill switch
    try f.server.manifest(v2Manifest, seed: moduleSeed)
    XCTAssertTrue(try mm.update().isFallback)
    XCTAssertTrue(f.reports.has("2", E7_ERR_SIGNATURE, "stale manifest"), "\(f.reports.lines)")
    try f.server.manifest(v2Manifest.replacingOccurrences(of: ",\"serial\":1", with: ""), seed: moduleSeed)
    XCTAssertTrue(try mm.update().isFallback, "manifest without serial after a serial was seen")
    // lifting the kill switch reuses the stored module
    f.server.fetched = []
    try f.server.manifest(v2Manifest.replacingOccurrences(of: "\"serial\":1", with: "\"serial\":3"), seed: moduleSeed)
    let b = try mm.update()
    XCTAssertFalse(b.isFallback)
    XCTAssertEqual(try answer(b), 2)
    XCTAssertFalse(f.server.fetched.contains(base + "app-2.e7b"))
  }

  func testRolloutExclusion() throws {
    // documented bucket: SHA-256("<version>:<userId>"), first 4 bytes big-endian, mod 10000 (same as Java)
    XCTAssertEqual(E7ModuleManager.rolloutBucket(userId: "user-42", version: "2"), 2213)
    XCTAssertEqual(E7ModuleManager.rolloutBucket(userId: "user-7", version: "2"), 4586)
    XCTAssertEqual(E7ModuleManager.rolloutBucket(userId: "alice", version: "3"), 6058)
    let f = try Fixture()  // user-42: bucket 2213
    let v2 = try module(2)
    try f.server.publish("2", v2, rollout: "0", serial: 1, seed: moduleSeed)
    let mm = try f.manager()
    XCTAssertTrue(try mm.update().isFallback)
    XCTAssertFalse(f.server.fetched.contains(base + "app-2.e7b"), "excluded: nothing downloaded")
    try f.server.publish("2", v2, rollout: "22", serial: 2, seed: moduleSeed)
    XCTAssertTrue(try mm.update().isFallback, "22 %: bucket 2213 excluded")
    try f.server.publish("2", v2, rollout: "22.14", serial: 3, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 2, "22.14 %: bucket 2213 included")
    XCTAssertEqual(f.reports.lines, [], "exclusion is not an error")
    try f.server.publish("3", try module(3), rollout: "1", serial: 4, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 2, "excluded from version 3: keeps version 2")
    let g = try Fixture()
    g.userId = "user-7"  // bucket 4586
    try g.server.publish("2", v2, rollout: "45.86", serial: 1, seed: moduleSeed)
    XCTAssertTrue(try g.manager().update().isFallback)
    try g.server.publish("2", v2, rollout: "45.87", serial: 2, seed: moduleSeed)
    XCTAssertEqual(try answer(try g.manager().update()), 2)
  }

  func testRequiresNewerVm() throws {
    let f = try Fixture()
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: moduleSeed, extra: ",\"minVm\":\"99.0\"")
    let mm = try f.manager()
    XCTAssertTrue(try mm.update().isFallback)
    XCTAssertTrue(f.reports.has("2", E7_ERR_VERSION, "requires e7vm 99.0.0"), "\(f.reports.lines)")
    XCTAssertFalse(f.server.fetched.contains(base + "app-2.e7b"))
    // the module header itself requires a newer VM (manifest without minVm)
    let newer = try E7VM.sign(withRequires(try E7VM.compile(source(3, selfTestOk: true), fileName: "app.ts"), 0x63_0000),
                              seed: moduleSeed)
    let info = E7VM.inspect(newer)
    XCTAssertEqual(info.status, st(E7_ERR_VERSION))
    XCTAssertEqual(info.requiresVersionString, "99.0.0")
    XCTAssertTrue(info.isSigned)
    try f.server.publish("3", newer, rollout: "100", serial: 2, seed: moduleSeed)
    XCTAssertTrue(try mm.update().isFallback)
    XCTAssertTrue(f.reports.has("3", E7_ERR_VERSION, "needs e7vm 99.0.0"), "\(f.reports.lines)")
    try f.server.publish("4", try module(4), rollout: "100", serial: 3, seed: moduleSeed,
                         extra: ",\"minVm\":\"\(E7VM.version)\"")
    XCTAssertEqual(try answer(try mm.update()), 4, "minVm = this VM")
  }

  func testSelfTestFailure() throws {
    let f = try Fixture()
    try f.server.publish("2", try module(2, selfTestOk: false), rollout: "100", serial: 1, seed: moduleSeed)
    let mm = try f.manager()
    XCTAssertTrue(try mm.update().isFallback)
    XCTAssertTrue(f.reports.has("2", E7_ERR_RUNTIME, "selfTest() returned"), "\(f.reports.lines)")
    try f.server.publish("3", try module(3), rollout: "100", serial: 2, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 3)
    try f.server.publish("4", try module(4, selfTestOk: false), rollout: "100", serial: 3, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 3, "failing selfTest of version 4: version 3 stays")
    f.server.offline = true
    XCTAssertEqual(try answer(try f.manager().start()), 3)
  }

  func testRollbackAfterFailures() throws {
    let f = try Fixture()
    let mm = try f.manager()
    try f.server.publish("2", try module(2), rollout: "100", serial: 1, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 2)
    try f.server.publish("3", try module(3), rollout: "100", serial: 2, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 3)
    try mm.reportFailure(status: st(E7_ERR_RUNTIME), message: "boom")
    try mm.reportFailure(status: st(E7_ERR_RUNTIME), message: "boom")
    mm.reportSuccess()  // resets the count
    try mm.reportFailure(status: st(E7_ERR_RUNTIME), message: "boom")
    try mm.reportFailure(status: st(E7_ERR_RUNTIME), message: "boom")
    XCTAssertEqual(try answer(mm.active), 3)
    XCTAssertEqual(f.reports.lines, [])
    try mm.reportFailure(status: st(E7_ERR_BUDGET), message: "too slow")
    XCTAssertEqual(try answer(mm.active), 2, "rolled back to 2")
    XCTAssertEqual(mm.active?.version, "2")
    XCTAssertTrue(f.reports.has("3", E7_ERR_BUDGET, "rolled back after 3 consecutive failures: too slow"), "\(f.reports.lines)")
    f.server.fetched = []
    XCTAssertEqual(try answer(try mm.update()), 2, "rolled-back version not taken again")
    XCTAssertFalse(f.server.fetched.contains(base + "app-3.e7b"))
    f.server.offline = true
    let mm2 = try f.manager()
    XCTAssertEqual(try answer(try mm2.start()), 2)
    for _ in 0..<3 { try mm2.reportFailure(status: st(E7_ERR_RUNTIME), message: "bad") }
    XCTAssertEqual(mm2.active?.isFallback, true, "version 2 fails too: fallback")
    XCTAssertTrue(try f.manager().start().isFallback)
    f.server.offline = false
    try f.server.publish("4", try module(4), rollout: "100", serial: 3, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm2.update()), 4, "a new version recovers")
  }

  // async exports through Active.callAsync; a selfTest that waits for a host operation is not activated
  func testAsyncExports() async throws {
    let f = try Fixture()
    f.setUp = { vm in
      try vm.registerAsyncNative("hostValue", signature: ">n") { _ in
        try await Task.sleep(nanoseconds: 1_000_000)
        return .number(7)
      }
    }
    let decl = "declare function hostValue(): Promise<number>;\n"
    let quote = "export async function quote(x: number): Promise<number> { return x + (await hostValue()); }\n"
    let v2 = decl + quote + "export function selfTest(): boolean { return true; }\nexport function answer(): number { return 2; }\n"
    try f.server.publish("2", try E7VM.sign(try E7VM.compile(v2, fileName: "app.ts"), seed: moduleSeed),
                         rollout: "100", serial: 1, seed: moduleSeed)
    let mm = try f.manager()
    let a = try mm.update()
    XCTAssertEqual(try answer(a), 2)
    let q = try await a.callAsync("quote", .number(1))
    XCTAssertEqual(q, .number(8))
    let v3 = decl + quote
      + "export async function selfTest(): Promise<boolean> { return (await hostValue()) === 7; }\n"
      + "export function answer(): number { return 3; }\n"
    try f.server.publish("3", try E7VM.sign(try E7VM.compile(v3, fileName: "app.ts"), seed: moduleSeed),
                         rollout: "100", serial: 2, seed: moduleSeed)
    XCTAssertEqual(try answer(try mm.update()), 2, "selfTest waits for a host operation: version 3 not activated")
    XCTAssertTrue(f.reports.has("3", E7_PENDING, "selfTest"), "\(f.reports.lines)")
  }
}
