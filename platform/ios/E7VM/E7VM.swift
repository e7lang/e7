// Swift wrapper around the e7vm C API (iOS / macOS).
// Swift Package Manager: depend on the `E7VM` product of https://github.com/e7lang/e7.git (the package
// compiles the VM from source as the `CE7VM` target). Alternatively link e7vm.xcframework (built by
// platform/ios/build_xcframework.sh), which exposes the same `CE7VM` module, and add this file.
//
//   let vm = try E7VM(trustedKeys: [serverPublicKey])
//   let module = try vm.load(bytecodeFromServer)
//   vm.keyProvider = { id in Keychain.loadKey(id) }
//   let sig = try vm.call(module, "sign", .string(payload), .handle(KeyHandle(1)))
import CE7VM
import Foundation

public struct E7Error: Error, CustomStringConvertible {
  public let status: Int32
  public let message: String
  public var description: String { "e7 error \(status): \(message)" }
  public var isSuspended: Bool { status == Int32(E7_SUSPENDED.rawValue) }
}

/// Opaque reference to a key held by the app (e.g. Keychain); resolved through `keyProvider`.
public struct KeyHandle: Equatable, Hashable {
  public let id: Int64
  public init(_ id: Int64) { self.id = id }
}

/// Header of a module, read without loading it (`E7VM.inspect`). Mirrors e7_module_info.
public struct E7ModuleInfo: Equatable {
  /// `E7_OK`: this VM can load it; `E7_ERR_VERSION`: format or required e7vm too new; `E7_ERR_LOAD`: not a module.
  public let status: Int32
  /// Bytecode format version (this VM: 2).
  public let format: UInt32
  /// Oldest e7vm that can run the module: major << 16 | minor << 8 | patch.
  public let requiresVersion: UInt32
  /// An Ed25519 signature trailer is present (not verified by `inspect`).
  public let isSigned: Bool
  /// First 8 bytes of SHA-256(public key) of the signing key (zero when unsigned).
  public let keyId: Data
  public let hasDebugInfo: Bool
  public var canRun: Bool { status == Int32(E7_OK.rawValue) }
  /// `requiresVersion` as text, e.g. "0.2.1".
  public var requiresVersionString: String { E7VM.versionString(requiresVersion) }
}

/// Garbage collector statistics since the VM was created (`E7VM.gcStats()`). Mirrors e7_gc_stats.
public struct E7GcStats: Equatable {
  public let collections: UInt64
  public let totalPauseNanoseconds: UInt64
  /// Longest single collection.
  public let maxPauseNanoseconds: UInt64
  /// Currently allocated (same as `memoryUsed`).
  public let heapBytes: Int
  public let peakHeapBytes: Int
}

public enum E7Value: Equatable {
  case number(Double), int(Int64), uint(UInt64), bool(Bool), string(String), bytes(Data), handle(KeyHandle), void
}

public final class E7Module {
  fileprivate let ptr: OpaquePointer
  fileprivate init(_ p: OpaquePointer) { ptr = p }
}

/// Owns byte buffers referenced by e7_value structs for the duration of a call.
private final class ArgBuffers {
  var buffers: [UnsafeMutablePointer<UInt8>] = []
  func copy(_ bytes: [UInt8]) -> (UnsafeMutablePointer<UInt8>, Int) {
    let p = UnsafeMutablePointer<UInt8>.allocate(capacity: max(bytes.count, 1))
    p.initialize(from: bytes, count: bytes.count)
    buffers.append(p)
    return (p, bytes.count)
  }
  deinit { buffers.forEach { $0.deallocate() } }
}

public final class E7VM {
  private let vm: OpaquePointer
  private var natives: [NativeBox] = []
  /// Key material handed to the VM by the key provider; must stay valid until the next callback.
  private var keyBuffer: UnsafeMutableBufferPointer<UInt8>?

  private func clearKeyBuffer() {
    if let b = keyBuffer {
      for i in b.indices { b[i] = 0 }
      b.deallocate()
      keyBuffer = nil
    }
  }

  /// Returns key material for a KeyHandle id (keys never become script values).
  public var keyProvider: ((Int64) -> Data?)? {
    didSet {
      let me = Unmanaged.passUnretained(self).toOpaque()
      if keyProvider == nil { e7_set_key_provider(vm, nil, nil); return }
      e7_set_key_provider(vm, { user, id, key, len in
        let this = Unmanaged<E7VM>.fromOpaque(user!).takeUnretainedValue()
        this.clearKeyBuffer()
        guard let d = this.keyProvider?(id) else { return -1 }
        let b = UnsafeMutableBufferPointer<UInt8>.allocate(capacity: max(d.count, 1))
        _ = b.initialize(from: d)
        this.keyBuffer = b
        key!.pointee = UnsafePointer(b.baseAddress)
        len!.pointee = d.count
        return 0
      }, me)
    }
  }

  public var printHandler: ((String) -> Void)? {
    didSet {
      let me = Unmanaged.passUnretained(self).toOpaque()
      if printHandler == nil { e7_set_print(vm, nil, nil); return }
      e7_set_print(vm, { user, text, len in
        let this = Unmanaged<E7VM>.fromOpaque(user!).takeUnretainedValue()
        let raw = UnsafeRawBufferPointer(start: text, count: len)
        this.printHandler?(String(decoding: raw, as: UTF8.self))
      }, me)
    }
  }

  /// - Parameter trustedKeys: Ed25519 public keys (32 bytes each). When non-empty only signed modules load.
  /// - Parameter gcGrowthPercent: next collection when the heap reaches this % of the live size after the previous
  ///   one (default 200, minimum 125; lower = less memory, more collections). 0 = default.
  public init(memoryLimit: Int = 0, suspendOnBudget: Bool = false, trustedKeys: [Data] = [], gcGrowthPercent: UInt32 = 0)
    throws
  {
    var cfg = e7_config()
    e7_config_init(&cfg)
    if memoryLimit > 0 { cfg.memory_limit = memoryLimit }
    if gcGrowthPercent > 0 { cfg.gc_growth_percent = gcGrowthPercent }
    cfg.suspend_on_budget = suspendOnBudget ? 1 : 0
    var keys = [UInt8]()
    for k in trustedKeys {
      guard k.count == 32 else {
        throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "public keys must be 32 bytes")
      }
      keys += [UInt8](k)
    }
    let created: OpaquePointer? = keys.withUnsafeBufferPointer { kb in
      var c = cfg
      if !keys.isEmpty {
        c.require_signature = 1
        c.trusted_keys = kb.baseAddress
        c.num_trusted_keys = keys.count / 32
      }
      return e7_vm_new(&c)  // keys are copied by the VM
    }
    guard let v = created else { throw E7Error(status: Int32(E7_ERR_MEMORY.rawValue), message: "cannot create VM") }
    vm = v
  }

  deinit {
    e7_vm_free(vm)
    clearKeyBuffer()
  }

  /// Library version, e.g. "0.2.1".
  public static var version: String { String(cString: e7_version()) }

  /// Library version as major << 16 | minor << 8 | patch (compare with `E7ModuleInfo.requiresVersion`).
  public static var versionNumber: UInt32 { e7_version_number() }

  static func versionString(_ v: UInt32) -> String { "\(v >> 16).\((v >> 8) & 0xFF).\(v & 0xFF)" }

  /// Reads a module's header without loading it, e.g. to skip modules this VM cannot run.
  public static func inspect(_ bytecode: Data) -> E7ModuleInfo {
    var info = e7_module_info()
    info.struct_size = MemoryLayout<e7_module_info>.size
    let bytes = [UInt8](bytecode)
    let st = bytes.withUnsafeBufferPointer { b -> e7_status in
      guard let base = b.baseAddress else { return E7_ERR_LOAD }
      return e7_inspect(base, b.count, &info)
    }
    let keyId = withUnsafeBytes(of: info.key_id) { Data($0) }
    return E7ModuleInfo(
      status: Int32(st.rawValue), format: info.format, requiresVersion: info.requires_version,
      isSigned: info.is_signed != 0, keyId: keyId, hasDebugInfo: info.has_debug_info != 0)
  }

  /// Garbage collector statistics (count, pause times, heap size and peak), e.g. for sizing the memory limit.
  public func gcStats() -> E7GcStats {
    var st = e7_gc_stats()
    st.struct_size = MemoryLayout<e7_gc_stats>.size
    _ = e7_gc_stats_get(vm, &st)
    return E7GcStats(
      collections: st.collections, totalPauseNanoseconds: st.total_pause_ns, maxPauseNanoseconds: st.max_pause_ns,
      heapBytes: st.heap_bytes, peakHeapBytes: st.peak_heap_bytes)
  }

  private func check(_ st: e7_status) throws {
    if st != E7_OK { throw E7Error(status: Int32(st.rawValue), message: String(cString: e7_last_error(vm))) }
  }

  public func load(_ bytecode: Data) throws -> E7Module {
    var m: OpaquePointer?
    let bytes = [UInt8](bytecode)
    let st = bytes.withUnsafeBufferPointer { e7_load(vm, $0.baseAddress, $0.count, &m) }
    try check(st)
    return E7Module(m!)
  }

  /// Compiles and loads source (development builds; requires the compiler in the library).
  public func loadSource(_ source: String, fileName: String = "main.ts") throws -> E7Module {
    var m: OpaquePointer?
    let utf8 = [CChar](source.utf8CString)
    let st = utf8.withUnsafeBufferPointer { e7_load_source(vm, $0.baseAddress, $0.count - 1, fileName, &m) }
    try check(st)
    return E7Module(m!)
  }

  public func hasFunction(_ module: E7Module, _ function: String) -> Bool { e7_has_function(module.ptr, function) != 0 }
  public func collectGarbage() { e7_collect_garbage(vm) }
  public func setBudget(_ ticks: Int64) { e7_set_budget(vm, ticks) }
  public func seedRandom(_ seed: UInt64) { e7_seed_random(vm, seed) }
  public var memoryUsed: Int { e7_memory_used(vm) }

  @discardableResult
  public func call(_ module: E7Module, _ function: String, _ args: E7Value...) throws -> E7Value {
    try call(module, function, arguments: args)
  }

  /// `call` with the arguments as an array.
  @discardableResult
  public func call(_ module: E7Module, _ function: String, arguments args: [E7Value]) throws -> E7Value {
    let bufs = ArgBuffers()
    var vals = args.map { E7VM.toC($0, bufs) }
    var ret = e7_value()
    let st = vals.withUnsafeMutableBufferPointer { e7_call(vm, module.ptr, function, $0.baseAddress, $0.count, &ret) }
    try check(st)
    return E7VM.fromC(ret)
  }

  public func resume() throws -> E7Value {
    var ret = e7_value()
    try check(e7_resume(vm, &ret))
    return E7VM.fromC(ret)
  }

  public func abortSuspended() { e7_abort_suspended(vm) }

  /// signature: parameter codes '>' return code, e.g. "sn>s" (see e7.h). Register before load().
  public func registerNative(_ name: String, signature: String, _ fn: @escaping ([E7Value]) throws -> E7Value) throws {
    let box = NativeBox(fn: fn)
    natives.append(box)
    let st = e7_register_native(vm, name, signature, { vm, args, n, ret, user in
      let box = Unmanaged<NativeBox>.fromOpaque(user!).takeUnretainedValue()
      var vals = [E7Value]()
      for i in 0..<n { vals.append(E7VM.fromC(args![i])) }
      do {
        let r = try box.fn(vals)
        box.retBuffers = ArgBuffers()  // previous return buffers were already copied by the VM
        ret!.pointee = E7VM.toC(r, box.retBuffers)
        return E7_OK
      } catch {
        e7_native_error(vm, "\(error)")
        return E7_ERR_RUNTIME
      }
    }, Unmanaged.passUnretained(box).toOpaque())
    try check(st)
  }

  public static func compile(_ source: String, fileName: String = "main.ts") throws -> Data {
    var out: UnsafeMutablePointer<UInt8>?
    var len = 0
    var errs: UnsafeMutablePointer<CChar>?
    let utf8 = [CChar](source.utf8CString)
    let st = utf8.withUnsafeBufferPointer { e7_compile($0.baseAddress, $0.count - 1, fileName, &out, &len, &errs) }
    defer {
      e7_free(out)
      e7_free(errs)
    }
    if st != E7_OK {
      throw E7Error(status: Int32(st.rawValue), message: errs.map { String(cString: $0) } ?? "compile error")
    }
    return Data(bytes: out!, count: len)
  }

  /// Appends an Ed25519 signature trailer to a module. `seed` is the 32-byte secret key.
  public static func sign(_ bytecode: Data, seed: Data) throws -> Data {
    guard seed.count == 32 else {
      throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "seed must be 32 bytes")
    }
    let code = [UInt8](bytecode)
    let sd = [UInt8](seed)
    var out: UnsafeMutablePointer<UInt8>?
    var len = 0
    let st = code.withUnsafeBufferPointer { c in
      sd.withUnsafeBufferPointer { s in e7_sign_module(c.baseAddress, c.count, s.baseAddress, &out, &len) }
    }
    defer { e7_free(out) }
    guard st == E7_OK, let o = out else {
      throw E7Error(status: Int32(st.rawValue), message: "signing failed")
    }
    return Data(bytes: o, count: len)
  }

  /// Ed25519 public key (32 bytes) for a 32-byte secret seed; pass it in `trustedKeys`.
  public static func publicKey(seed: Data) throws -> Data {
    guard seed.count == 32 else {
      throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "seed must be 32 bytes")
    }
    let sd = [UInt8](seed)
    var pk = [UInt8](repeating: 0, count: 32)
    sd.withUnsafeBufferPointer { s in pk.withUnsafeMutableBufferPointer { p in e7_public_key(s.baseAddress, p.baseAddress) } }
    return Data(pk)
  }

  /// Detached Ed25519 signature (64 bytes) of arbitrary data, e.g. an update manifest (e7_sign_detached).
  public static func signDetached(_ data: Data, seed: Data) throws -> Data {
    guard seed.count == 32 else {
      throw E7Error(status: Int32(E7_ERR_ARGS.rawValue), message: "seed must be 32 bytes")
    }
    let d = [UInt8](data), sd = [UInt8](seed)
    var sig = [UInt8](repeating: 0, count: 64)
    let st = d.withUnsafeBufferPointer { db in
      sd.withUnsafeBufferPointer { s in
        sig.withUnsafeMutableBufferPointer { o in e7_sign_detached(db.baseAddress, db.count, s.baseAddress, o.baseAddress) }
      }
    }
    guard st == E7_OK else { throw E7Error(status: Int32(st.rawValue), message: "signing failed") }
    return Data(sig)
  }

  /// True if `signature` is a detached signature of `data` by one of the public keys (32 bytes each).
  public static func verifyDetached(_ data: Data, signature: Data, trustedKeys: [Data]) -> Bool {
    guard signature.count == 64, !trustedKeys.isEmpty, trustedKeys.allSatisfy({ $0.count == 32 }) else { return false }
    let d = [UInt8](data), sig = [UInt8](signature), keys = trustedKeys.flatMap { [UInt8]($0) }
    let st = d.withUnsafeBufferPointer { db in
      sig.withUnsafeBufferPointer { sb in
        keys.withUnsafeBufferPointer { kb in
          e7_verify_detached(db.baseAddress, db.count, sb.baseAddress, kb.baseAddress, trustedKeys.count)
        }
      }
    }
    return st == E7_OK
  }

  // MARK: - marshaling

  fileprivate static func toC(_ a: E7Value, _ bufs: ArgBuffers) -> e7_value {
    var v = e7_value()
    switch a {
    case .number(let d): v.type = E7_NUMBER; v.v.num = d
    case .int(let x): v.type = E7_INT; v.v.i = x
    case .uint(let x): v.type = E7_UINT; v.v.u = x
    case .bool(let b): v.type = E7_BOOL; v.v.boolean = b ? 1 : 0
    case .string(let s):
      let (p, n) = bufs.copy(Array(s.utf8))
      v.type = E7_STRING
      v.v.str.ptr = UnsafeRawPointer(p).assumingMemoryBound(to: CChar.self)
      v.v.str.len = n
    case .bytes(let d):
      let (p, n) = bufs.copy([UInt8](d))
      v.type = E7_BYTES
      v.v.bytes.ptr = UnsafePointer(p)
      v.v.bytes.len = n
    case .handle(let h): v.type = E7_HANDLE; v.v.handle = h.id
    case .void: v.type = E7_VOID
    }
    return v
  }

  fileprivate static func fromC(_ v: e7_value) -> E7Value {
    switch v.type {
    case E7_NUMBER: return .number(v.v.num)
    case E7_INT: return .int(v.v.i)
    case E7_UINT: return .uint(v.v.u)
    case E7_BOOL: return .bool(v.v.boolean != 0)
    case E7_STRING:
      let raw = UnsafeRawBufferPointer(start: v.v.str.ptr, count: v.v.str.len)
      return .string(String(decoding: raw, as: UTF8.self))
    case E7_BYTES:
      return .bytes(v.v.bytes.len == 0 ? Data() : Data(bytes: v.v.bytes.ptr, count: v.v.bytes.len))
    case E7_HANDLE: return .handle(KeyHandle(v.v.handle))
    default: return .void
    }
  }
}

private final class NativeBox {
  let fn: ([E7Value]) throws -> E7Value
  var retBuffers = ArgBuffers()
  init(fn: @escaping ([E7Value]) throws -> E7Value) { self.fn = fn }
}
