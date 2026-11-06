// Over-the-air delivery of E7 modules: signed manifest, staged rollout, kill switch, last-known-good and
// rollback (iOS / macOS). Same behaviour as org.e7vm.E7ModuleManager (Java / Kotlin); see docs/DEPLOYMENT.md.
//
//   var cfg = E7ModuleManager.Configuration(
//     manifestURL: URL(string: "https://updates.example.com/pricing/manifest.json")!,
//     trustedKeys: [releaseKey, nextReleaseKey],              // Ed25519 public keys (32 bytes)
//     storageDirectory: appSupport.appendingPathComponent("e7"),
//     userId: installationId,                                 // stable per user / install
//     fallbackModule: bundledModule, fallbackVersion: "bundled-1.4")
//   cfg.memoryLimit = 16 << 20
//   cfg.setUpVM = { vm in vm.setBudget(5_000_000); try vm.registerNative(...) }
//   cfg.reportError = { version, status, message in telemetry.log(version, status, message) }
//   let mm = try E7ModuleManager(configuration: cfg, fetcher: { url in try httpGet(url) })
//   mm.start()                                  // at launch: last-known-good from disk, or the fallback
//   DispatchQueue.global().async { mm.update() } // manifest, download, verify, self-test, activate
//   let price = try mm.active!.call("price", .string(order))
//   let quote = try await mm.active!.callAsync("quote", .string(order))   // awaits async host functions
//   mm.reportSuccess()                          // or mm.reportFailure(status:message:)
//
// start() / update() load modules and run selfTest synchronously: a module whose top-level code or selfTest
// waits for an async host function is reported (E7_PENDING) and not activated; its exports may be async.
//
// The manifest is JSON {version, url, sha256, rolloutPercent, killSwitch, minVm?, serial?} with a detached
// signature (hex, `e7c --sign-file` / tools/manifest.py) at "<manifest URL>.sig". Manifests that do not verify
// against the trusted keys are ignored, so a kill switch or rollout cannot be forged. Downloaded modules must be
// signed by a trusted key too; the bundled fallback is not checked (it is part of the app).
//
// The manager owns its VMs (a replaced VM is released). Its methods are serialized by a lock; like a VM, do not
// call into the active module while update() may replace it on another thread.
import CE7VM
import CryptoKit
import Foundation

public final class E7ModuleManager {
  /// Downloads a URL (manifest, signature, module) with the app's own networking stack. Called synchronously.
  public typealias Fetcher = (URL) throws -> Data
  /// Problems the manager handled: (module version or nil if unknown, e7_status code, message).
  public typealias ErrorReporter = (_ moduleVersion: String?, _ status: Int32, _ message: String) -> Void

  public struct Configuration {
    public var manifestURL: URL
    /// Ed25519 public keys (32 bytes each) trusted for manifests and modules; several during a key rotation.
    public var trustedKeys: [Data]
    /// Private directory for the last-known-good modules and the manager's state.
    public var storageDirectory: URL
    /// Stable id of the user or installation; decides the rollout bucket.
    public var userId: String
    /// The module bundled with the app: used on a kill switch or when no downloaded module is usable.
    public var fallbackModule: Data
    public var fallbackVersion: String
    public var memoryLimit: Int = 0
    public var gcGrowthPercent: UInt32 = 0
    /// Prepares every new VM before the module loads: natives, key provider, print handler, budget.
    public var setUpVM: (E7VM) throws -> Void = { _ in }
    public var reportError: ErrorReporter = { _, _, _ in }
    /// Consecutive failures (reportFailure) after which a downloaded module is rolled back.
    public var maxConsecutiveFailures = 3
    /// Exported function that must return true before a downloaded module is activated (nil: none).
    public var selfTestFunction: String? = "selfTest"

    public init(manifestURL: URL, trustedKeys: [Data], storageDirectory: URL, userId: String, fallbackModule: Data,
                fallbackVersion: String = "fallback") {
      self.manifestURL = manifestURL
      self.trustedKeys = trustedKeys
      self.storageDirectory = storageDirectory
      self.userId = userId
      self.fallbackModule = fallbackModule
      self.fallbackVersion = fallbackVersion
    }
  }

  /// The module currently in use.
  public final class Active {
    public let vm: E7VM
    public let module: E7Module
    /// Version from the manifest, or the fallback's version label.
    public let version: String
    /// Hex SHA-256 of the module file; nil for the fallback.
    public let sha256: String?
    public let isFallback: Bool

    init(vm: E7VM, module: E7Module, version: String, sha256: String?, isFallback: Bool) {
      self.vm = vm
      self.module = module
      self.version = version
      self.sha256 = sha256
      self.isFallback = isFallback
    }

    /// Calls an exported function of the active module.
    @discardableResult
    public func call(_ function: String, _ args: E7Value...) throws -> E7Value {
      try vm.call(module, function, arguments: args)
    }

    #if compiler(>=6.0)
      /// `E7VM.callAsync` on the active module (exports that await async host functions).
      @discardableResult
      public func callAsync(_ function: String, _ args: E7Value..., isolation: isolated (any Actor)? = #isolation)
        async throws -> E7Value
      {
        try await vm.callAsync(module, function, arguments: args, isolation: isolation)
      }
    #else
      /// `E7VM.callAsync` on the active module (exports that await async host functions).
      @discardableResult
      public func callAsync(_ function: String, _ args: E7Value...) async throws -> E7Value {
        try await vm.callAsync(module, function, arguments: args)
      }
    #endif
  }

  private struct Entry: Codable, Equatable {
    var version: String
    var sha256: String
  }

  private struct State: Codable {
    var current: Entry?       // last-known-good
    var previous: Entry?      // the one before it
    var failures = 0
    var killed = false
    var serial: Int64?        // newest manifest serial accepted
    var bad: [String] = []    // sha256 of rolled-back / broken modules
  }

  private struct Manifest {
    var version: String?
    var url = ""
    var sha256 = ""
    var rolloutPercent = 0.0
    var killSwitch = false
    var minVm: UInt32 = 0
    var serial: Int64?
  }

  private struct Failure: Error {
    let status: Int32
    let message: String
  }

  private let config: Configuration
  private let fetcher: Fetcher
  private let lock = NSLock()
  private var state = State()
  private var activeModule: Active?
  private var modulesDirectory: URL { config.storageDirectory.appendingPathComponent("modules", isDirectory: true) }
  private var stateURL: URL { config.storageDirectory.appendingPathComponent("state.json") }
  private var signatureURL: URL { URL(string: config.manifestURL.absoluteString + ".sig")! }

  public init(configuration: Configuration, fetcher: @escaping Fetcher) throws {
    guard !configuration.trustedKeys.isEmpty, configuration.trustedKeys.allSatisfy({ $0.count == 32 }) else {
      throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "at least one 32-byte trusted key is required")
    }
    guard configuration.maxConsecutiveFailures >= 1 else {
      throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "maxConsecutiveFailures must be at least 1")
    }
    config = configuration
    self.fetcher = fetcher
    if let d = try? Data(contentsOf: stateURL), let s = try? JSONDecoder().decode(State.self, from: d) { state = s }
  }

  /// The module in use (nil before `start()` / `update()`).
  public var active: Active? {
    lock.lock()
    defer { lock.unlock() }
    return activeModule
  }

  /// Activates the last-known-good module from disk without network access (or the fallback when there is
  /// none, it no longer loads, or a kill switch is in effect). Call at app start. Throws only if the bundled
  /// fallback module does not load.
  @discardableResult
  public func start() throws -> Active {
    lock.lock()
    defer { lock.unlock() }
    try activateFromDisk()
    return activeModule!
  }

  /// Fetches and verifies the manifest; downloads, verifies, self-tests and activates the module it names when
  /// this client is in the rollout. On any problem the current module stays active (the problem goes to
  /// `reportError`). Blocks while downloading: call it from a background queue. Throws only if the bundled
  /// fallback module does not load.
  @discardableResult
  public func update() throws -> Active {
    lock.lock()
    defer { lock.unlock() }
    if activeModule == nil { try activateFromDisk() }
    let mf: Manifest
    do {
      let body = try fetcher(config.manifestURL)
      guard let sig = Self.parseSignature(try fetcher(signatureURL)),
        E7VM.verifyDetached(body, signature: sig, trustedKeys: config.trustedKeys)
      else {
        report(nil, E7_ERR_SIGNATURE, "manifest signature is not valid for the trusted keys")
        return activeModule!
      }
      mf = try Self.parseManifest(body)
    } catch {
      report(nil, E7_ERR_LOAD, "manifest: \(Self.describe(error))")
      return activeModule!
    }
    // replay protection: once a manifest with a serial was accepted, older (or serial-less) ones are ignored
    if let newest = state.serial, (mf.serial ?? Int64.min) < newest {
      report(mf.version, E7_ERR_SIGNATURE,
             "stale manifest ignored (serial \(mf.serial.map(String.init) ?? "missing"), newest accepted \(newest))")
      return activeModule!
    }
    if let s = mf.serial { state.serial = s }
    state.killed = mf.killSwitch
    saveState()
    if mf.killSwitch {
      if !activeModule!.isFallback { try activateFallback() }
      return activeModule!
    }
    let version = mf.version ?? ""
    if Self.rolloutBucket(userId: config.userId, version: version) >= Int((mf.rolloutPercent * 100).rounded()) {
      return try keepCurrent()
    }
    if mf.minVm > E7VM.versionNumber {
      report(version, E7_ERR_VERSION,
             "module requires e7vm \(E7VM.versionString(mf.minVm)), this is \(E7VM.version)")
      return try keepCurrent()
    }
    if state.bad.contains(mf.sha256) { return try keepCurrent() }  // rolled back before: wait for a new version
    if let a = activeModule, !a.isFallback, a.sha256 == mf.sha256 { return a }  // up to date
    var code = readCached(mf.sha256)
    if code == nil {
      let downloaded: Data
      do {
        guard let url = URL(string: mf.url, relativeTo: config.manifestURL)?.absoluteURL else {
          throw Failure(status: Int32(E7_ERR_LOAD.rawValue), message: "invalid url \(mf.url)")
        }
        downloaded = try fetcher(url)
      } catch {
        report(version, E7_ERR_LOAD, "download failed: \(Self.describe(error))")
        return try keepCurrent()
      }
      let got = Self.sha256Hex(downloaded)
      guard got == mf.sha256 else {
        report(version, E7_ERR_SIGNATURE, "sha256 mismatch: manifest \(mf.sha256), downloaded \(got)")
        return try keepCurrent()
      }
      code = downloaded
    }
    guard let next = instantiateChecked(code!, version: version, sha256: mf.sha256) else { return try keepCurrent() }
    do {
      try FileManager.default.createDirectory(at: modulesDirectory, withIntermediateDirectories: true)
      try code!.write(to: moduleURL(mf.sha256), options: .atomic)
    } catch {
      report(version, E7_ERR_STATE, "cannot store the module: \(Self.describe(error))")
    }
    if let cur = state.current, cur.sha256 != mf.sha256 { state.previous = cur }
    state.current = Entry(version: version, sha256: mf.sha256)
    state.failures = 0
    saveState()
    switchTo(next)
    return next
  }

  /// The host saw the active module work: resets the failure count.
  public func reportSuccess() {
    lock.lock()
    defer { lock.unlock() }
    if state.failures != 0 {
      state.failures = 0
      saveState()
    }
  }

  /// The host saw the active module fail. After `maxConsecutiveFailures` consecutive failures a downloaded
  /// module is rolled back to the previous good one, or the fallback, and is not activated again.
  public func reportFailure(status: Int32, message: String) throws {
    lock.lock()
    defer { lock.unlock() }
    guard let a = activeModule, !a.isFallback, let sha = a.sha256 else { return }
    state.failures += 1
    if state.failures < config.maxConsecutiveFailures {
      saveState()
      return
    }
    report(a.version, status, "rolled back after \(state.failures) consecutive failures: \(message)")
    markBad(sha)
    state.failures = 0
    saveState()
    try activateFromDisk()
  }

  /// Rollout bucket 0..9999 of a user for a module version: the first 4 bytes of SHA-256(UTF-8 of
  /// version + ":" + userId) as a big-endian unsigned number, modulo 10000. A client takes the version when
  /// bucket < rolloutPercent * 100.
  public static func rolloutBucket(userId: String, version: String) -> Int {
    let h = Array(SHA256.hash(data: Data((version + ":" + userId).utf8)))
    let v = UInt32(h[0]) << 24 | UInt32(h[1]) << 16 | UInt32(h[2]) << 8 | UInt32(h[3])
    return Int(v % 10000)
  }

  // MARK: - activation

  private func keepCurrent() throws -> Active {
    if activeModule == nil || activeModule!.isFallback { try activateFromDisk() }
    return activeModule!
  }

  // Last-known-good, then the previous module, then the fallback. Unusable entries are dropped.
  private func activateFromDisk() throws {
    if !state.killed {
      for _ in 0..<2 {
        guard let e = state.current else { break }
        if let a = activeModule, !a.isFallback, a.sha256 == e.sha256 { return }
        var next: Active?
        if !state.bad.contains(e.sha256) {
          if let code = readCached(e.sha256) {
            next = instantiateChecked(code, version: e.version, sha256: e.sha256)
          } else {
            report(e.version, E7_ERR_LOAD, "stored module is missing or corrupted")
          }
          if next == nil { markBad(e.sha256) }
        }
        if let n = next {
          saveState()
          switchTo(n)
          return
        }
        state.current = state.previous  // drop the unusable module
        state.previous = nil
        state.failures = 0
        saveState()
      }
    }
    if activeModule == nil || !activeModule!.isFallback { try activateFallback() }
  }

  private func activateFallback() throws {
    do {
      let vm = try newVM(requireSignature: false)
      let m = try vm.load(config.fallbackModule)
      switchTo(Active(vm: vm, module: m, version: config.fallbackVersion, sha256: nil, isFallback: true))
    } catch {
      let st = (error as? E7Error)?.status ?? Int32(E7_ERR_STATE.rawValue)
      config.reportError(config.fallbackVersion, st, "the fallback module does not load: \(Self.describe(error))")
      throw error
    }
  }

  // inspect + signed load + selfTest; reports and returns nil on failure
  private func instantiateChecked(_ code: Data, version: String, sha256: String) -> Active? {
    let info = E7VM.inspect(code)
    guard info.canRun else {
      config.reportError(
        version, info.status,
        info.status == Int32(E7_ERR_VERSION.rawValue)
          ? "module needs e7vm \(info.requiresVersionString) (format \(info.format)), this is \(E7VM.version)"
          : "not a valid e7 module")
      return nil
    }
    do {
      let vm = try newVM(requireSignature: true)
      let m = try vm.load(code)
      if let fn = config.selfTestFunction, vm.hasFunction(m, fn) {
        let r = try vm.call(m, fn)
        guard r == .bool(true) else {
          throw Failure(status: Int32(E7_ERR_RUNTIME.rawValue), message: "\(fn)() returned \(r)")
        }
      }
      return Active(vm: vm, module: m, version: version, sha256: sha256, isFallback: false)
    } catch {
      let st = (error as? E7Error)?.status ?? (error as? Failure)?.status ?? Int32(E7_ERR_RUNTIME.rawValue)
      config.reportError(version, st, Self.describe(error))
      return nil
    }
  }

  private func newVM(requireSignature: Bool) throws -> E7VM {
    let vm = try E7VM(memoryLimit: config.memoryLimit, trustedKeys: requireSignature ? config.trustedKeys : [],
                      gcGrowthPercent: config.gcGrowthPercent)
    try config.setUpVM(vm)
    return vm
  }

  private func switchTo(_ next: Active) {
    activeModule = next  // the previous VM is released with its Active
    cleanModules()
  }

  private func markBad(_ sha: String) {
    if !state.bad.contains(sha) { state.bad.append(sha) }
    if state.bad.count > 32 { state.bad.removeFirst(state.bad.count - 32) }
    if state.current?.sha256 == sha, activeModule?.sha256 == sha {
      state.current = state.previous
      state.previous = nil
    } else if state.previous?.sha256 == sha {
      state.previous = nil
    }
  }

  private func report(_ version: String?, _ status: e7_status, _ message: String) {
    config.reportError(version, Int32(status.rawValue), message)
  }

  private func report(_ version: String?, _ status: Int32, _ message: String) {
    config.reportError(version, status, message)
  }

  private static func describe(_ error: Error) -> String {
    if let e = error as? E7Error { return e.message }
    if let f = error as? Failure { return f.message }
    return "\(error)"
  }

  // MARK: - storage

  private func moduleURL(_ sha: String) -> URL { modulesDirectory.appendingPathComponent(sha + ".e7b") }

  private func readCached(_ sha: String) -> Data? {
    guard let d = try? Data(contentsOf: moduleURL(sha)), Self.sha256Hex(d) == sha else { return nil }
    return d
  }

  private func cleanModules() {
    let keep = Set([state.current?.sha256, state.previous?.sha256].compactMap { $0.map { $0 + ".e7b" } })
    let files = (try? FileManager.default.contentsOfDirectory(atPath: modulesDirectory.path)) ?? []
    for f in files where !keep.contains(f) {
      try? FileManager.default.removeItem(at: modulesDirectory.appendingPathComponent(f))
    }
  }

  private func saveState() {
    do {
      try FileManager.default.createDirectory(at: config.storageDirectory, withIntermediateDirectories: true)
      try JSONEncoder().encode(state).write(to: stateURL, options: .atomic)
    } catch {
      report(nil, E7_ERR_STATE, "cannot save the module state: \(Self.describe(error))")
    }
  }

  // MARK: - hashing, signatures, manifest

  static func sha256Hex(_ data: Data) -> String {
    SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
  }

  // "<manifest>.sig": 128 hex digits (e7c --sign-file), surrounding whitespace allowed; or 64 raw bytes
  static func parseSignature(_ raw: Data) -> Data? {
    if raw.count == 64 { return raw }
    guard let text = String(data: raw, encoding: .ascii)?.trimmingCharacters(in: .whitespacesAndNewlines),
      text.count == 128
    else { return nil }
    var out = Data(capacity: 64)
    var i = text.startIndex
    while i < text.endIndex {
      let j = text.index(i, offsetBy: 2)
      guard let b = UInt8(text[i..<j], radix: 16) else { return nil }
      out.append(b)
      i = j
    }
    return out
  }

  private static func isBool(_ v: Any) -> Bool {
    guard let n = v as? NSNumber else { return false }
    return CFGetTypeID(n) == CFBooleanGetTypeID()
  }

  private static func number(_ v: Any?) -> Double? {
    guard let v = v, !isBool(v), let n = v as? NSNumber else { return nil }
    return n.doubleValue
  }

  private static func bad(_ message: String) -> Failure { Failure(status: Int32(E7_ERR_LOAD.rawValue), message: message) }

  private static func parseManifest(_ body: Data) throws -> Manifest {
    guard let o = try JSONSerialization.jsonObject(with: body) as? [String: Any] else { throw bad("not a JSON object") }
    var m = Manifest()
    if let k = o["killSwitch"], !(k is NSNull) {
      guard isBool(k) else { throw bad("killSwitch must be a boolean") }
      m.killSwitch = (k as! NSNumber).boolValue
    }
    if let s = o["serial"], !(s is NSNull) {
      guard let d = number(s), d == d.rounded(), abs(d) < 9.0e15 else { throw bad("serial must be an integer") }
      m.serial = Int64(d)
    }
    m.version = o["version"] as? String
    if m.killSwitch { return m }
    guard !(m.version ?? "").isEmpty else { throw bad("missing version") }
    guard let url = o["url"] as? String else { throw bad("missing url") }
    m.url = url
    guard let h = (o["sha256"] as? String)?.lowercased(), h.count == 64, h.allSatisfy({ $0.isHexDigit }) else {
      throw bad("sha256 must be 64 hex digits")
    }
    m.sha256 = h
    guard let r = number(o["rolloutPercent"]), r >= 0, r <= 100 else {
      throw bad("rolloutPercent must be a number from 0 to 100")
    }
    m.rolloutPercent = r
    if let mv = o["minVm"], !(mv is NSNull) {
      if let s = mv as? String {
        guard let parsed = parseVersion(s) else { throw bad("invalid minVm \(s)") }
        m.minVm = parsed
      } else if let d = number(mv), d >= 0, d <= Double(UInt32.max) {
        m.minVm = UInt32(d)
      } else {
        throw bad("minVm must be a version string")
      }
    }
    return m
  }

  // "1.2", "1.2.3", "v1.2" -> major << 16 | minor << 8 | patch
  static func parseVersion(_ s: String) -> UInt32? {
    let t = s.hasPrefix("v") ? String(s.dropFirst()) : s
    let parts = t.split(separator: ".", omittingEmptySubsequences: false)
    guard parts.count == 2 || parts.count == 3 else { return nil }
    var v: UInt32 = 0
    for i in 0..<3 {
      var x: UInt32 = 0
      if i < parts.count {
        guard parts[i].count <= 3, parts[i].allSatisfy({ $0.isASCII && $0.isNumber }), let n = UInt32(parts[i]),
          n <= 255
        else { return nil }
        x = n
      }
      v = v << 8 | x
    }
    return v
  }
}
