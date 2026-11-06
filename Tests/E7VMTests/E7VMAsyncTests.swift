// Async host functions: registerAsyncNative, callAsync, loadAsync (same scenarios as
// platform/android/shared-test/org/e7vm/AsyncScenarios.java).
import CE7VM
import E7VM
import Foundation
import XCTest

private let app = """
  declare function fetchKey(id: string): Promise<Uint8Array>;
  declare function now(): Promise<number>;
  export async function keyLength(id: string): Promise<number> {
    const k = await fetchKey(id);
    return k.length + (await now());
  }
  export async function both(): Promise<string> {
    const order: string[] = [];
    const pa = fetchKey("a").then((k) => { order.push("a"); return k; });
    const pb = fetchKey("b").then((k) => { order.push("b"); return k; });
    const [a, b] = await Promise.all([pa, pb]);
    return `${a.length},${b.length},${order.join("")}`;
  }
  export async function failing(): Promise<number> {
    try {
      await fetchKey("x");
      return 1;
    } catch (e) {
      if (e instanceof Error) console.log("caught " + e.message);
      return -1;
    }
  }
  export async function stamp(): Promise<number> { return (await now()) * 2; }
  export function syncCall(): number { return 3; }
  """

private struct HostError: Error, CustomStringConvertible {
  let text: String
  var description: String { text }
}

private func status(_ s: e7_status) -> Int32 { Int32(s.rawValue) }

/// One call of the async native fetchKey: the test completes it, from any thread.
private final class Op: @unchecked Sendable {
  let key: String
  private let lock = NSLock()
  private var waiter: CheckedContinuation<E7Value, Error>?
  private var outcome: Result<E7Value, Error>?

  init(_ key: String) { self.key = key }

  func value() async throws -> E7Value {
    try await withCheckedThrowingContinuation { (c: CheckedContinuation<E7Value, Error>) in
      lock.lock()
      if let r = outcome {
        lock.unlock()
        c.resume(with: r)
      } else {
        waiter = c
        lock.unlock()
      }
    }
  }

  func complete(_ r: Result<E7Value, Error>) {
    lock.lock()
    let w = waiter
    waiter = nil
    if w == nil { outcome = r }
    lock.unlock()
    w?.resume(with: r)
  }

  /// Completes it later on another thread.
  func completeLater(_ r: Result<E7Value, Error>, after seconds: Double = 0.03) {
    DispatchQueue.global().asyncAfter(deadline: .now() + seconds) { self.complete(r) }
  }
}

/// A VM with the async natives fetchKey (completed by the test) and now (returns at once).
private final class Host {
  let vm: E7VM
  let module: E7Module
  var printed: [String] = []
  private var started: AsyncStream<Op>.Iterator

  init() throws {
    vm = try E7VM()
    var cont: AsyncStream<Op>.Continuation!
    let stream = AsyncStream<Op> { cont = $0 }
    started = stream.makeAsyncIterator()
    let opened = cont!
    try vm.registerAsyncNative("fetchKey", signature: "s>y") { args in
      guard case .string(let key) = args[0] else { throw HostError(text: "bad argument") }
      let op = Op(key)
      opened.yield(op)
      return try await op.value()
    }
    try vm.registerAsyncNative("now", signature: ">n") { _ in .number(5) }
    module = try vm.loadSource(app, fileName: "app.ts")
    vm.printHandler = { [unowned self] in self.printed.append($0) }
  }

  /// The next fetchKey call.
  func next() async -> Op {
    await started.next()!
  }
}

@MainActor
final class E7VMAsyncTests: XCTestCase {
  func testSettledLaterFromAnotherThread() async throws {
    let h = try Host()
    let call = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k1")) }
    let op = await h.next()
    XCTAssertEqual(op.key, "k1")
    XCTAssertEqual(h.vm.openTickets, 1)
    op.completeLater(.success(.bytes(Data([1, 2, 3, 4]))))
    let r = try await call.value
    XCTAssertEqual(r, .number(9))  // 4 + 5
    XCTAssertEqual(h.vm.openTickets, 0)
  }

  func testSettledAtOnce() async throws {
    let h = try Host()
    // now() returns without suspending: still settled through the completion stream
    let r = try await h.vm.callAsync(h.module, "stamp")
    XCTAssertEqual(r, .number(10))
    // a synchronous call does not start async natives: it reports E7_PENDING and abandons the call
    do {
      try h.vm.call(h.module, "stamp")
      XCTFail("expected E7_PENDING")
    } catch let e as E7Error {
      XCTAssertTrue(e.isPending, "\(e)")
      XCTAssertTrue(e.message.contains("callAsync"), e.message)
    }
    XCTAssertEqual(try h.vm.call(h.module, "syncCall"), .number(3), "the VM is usable after the PENDING error")
    // completed on the test's own thread
    let call = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k")) }
    let op = await h.next()
    op.complete(.success(.bytes(Data([7, 7]))))
    let r2 = try await call.value
    XCTAssertEqual(r2, .number(7))
  }

  func testRejectedAndCaught() async throws {
    let h = try Host()
    let call = Task { try await h.vm.callAsync(h.module, "failing") }
    let op = await h.next()
    op.completeLater(.failure(HostError(text: "no such key")))
    let r = try await call.value
    XCTAssertEqual(r, .number(-1))
    XCTAssertEqual(h.printed, ["caught no such key"])
    // an uncaught rejection fails the call
    let call2 = Task { try await h.vm.callAsync(h.module, "keyLength", .string("gone")) }
    await h.next().complete(.failure(HostError(text: "gone")))
    do {
      _ = try await call2.value
      XCTFail("expected a runtime error")
    } catch let e as E7Error {
      XCTAssertEqual(e.status, status(E7_ERR_RUNTIME))
      XCTAssertTrue(e.message.contains("gone"), e.message)
    }
    // a value of the wrong type rejects the promise
    let call3 = Task { try await h.vm.callAsync(h.module, "keyLength", .string("bad")) }
    await h.next().complete(.success(.string("not bytes")))
    do {
      _ = try await call3.value
      XCTFail("expected a runtime error")
    } catch let e as E7Error {
      XCTAssertEqual(e.status, status(E7_ERR_RUNTIME))
      XCTAssertTrue(e.message.contains("fetchKey"), e.message)
    }
  }

  func testPromiseAllReverseOrder() async throws {
    let h = try Host()
    let call = Task { try await h.vm.callAsync(h.module, "both") }
    let a = await h.next(), b = await h.next()
    XCTAssertEqual([a.key, b.key], ["a", "b"])
    b.completeLater(.success(.bytes(Data(count: 4))), after: 0.01)
    a.completeLater(.success(.bytes(Data(count: 1))), after: 0.15)
    let r = try await call.value
    XCTAssertEqual(r, .string("1,4,ba"), "Promise.all keeps the order; b was settled first")
  }

  func testPendingCallBlocksSecondCall() async throws {
    let h = try Host()
    let call = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k1")) }
    let op = await h.next()
    do {
      try h.vm.call(h.module, "syncCall")
      XCTFail("expected E7_ERR_STATE")
    } catch let e as E7Error {
      XCTAssertEqual(e.status, status(E7_ERR_STATE))
    }
    do {
      try await h.vm.callAsync(h.module, "syncCall")
      XCTFail("expected E7_ERR_STATE")
    } catch let e as E7Error {
      XCTAssertEqual(e.status, status(E7_ERR_STATE))
    }
    op.complete(.success(.bytes(Data([1]))))
    let r = try await call.value
    XCTAssertEqual(r, .number(6))
    XCTAssertEqual(try h.vm.call(h.module, "syncCall"), .number(3))
  }

  func testAbandonPendingCall() async throws {
    let h = try Host()
    let call = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k1")) }
    let op = await h.next()
    call.cancel()
    do {
      _ = try await call.value
      XCTFail("expected CancellationError")
    } catch is CancellationError {
    }
    XCTAssertEqual(h.vm.openTickets, 0)
    XCTAssertEqual(try h.vm.call(h.module, "syncCall"), .number(3), "the VM is free after cancelling")
    op.complete(.success(.bytes(Data([1]))))  // a late completion is ignored
    // abortSuspended() abandons it too
    let call2 = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k2")) }
    let op2 = await h.next()
    h.vm.abortSuspended()
    do {
      _ = try await call2.value
      XCTFail("expected CancellationError")
    } catch is CancellationError {
    }
    op2.complete(.success(.bytes(Data([1]))))
    let call3 = Task { try await h.vm.callAsync(h.module, "keyLength", .string("k3")) }
    let op3 = await h.next()
    XCTAssertEqual(op3.key, "k3")
    op3.complete(.success(.bytes(Data(count: 3))))
    let r = try await call3.value
    XCTAssertEqual(r, .number(8))
  }

  func testTopLevelAwait() async throws {
    let src = """
      declare function fetchKey(id: string): Promise<Uint8Array>;
      console.log("init");
      const key = await fetchKey("boot");
      export function keySize(): number { return key.length; }
      """
    let code = try E7VM.compile(src, fileName: "boot.ts")
    let vm = try E7VM()
    var printed: [String] = []
    vm.printHandler = { printed.append($0) }
    try vm.registerAsyncNative("fetchKey", signature: "s>y") { _ in
      try await Task.sleep(nanoseconds: 10_000_000)
      return .bytes(Data(count: 16))
    }
    do {
      _ = try vm.load(code)
      XCTFail("expected E7_PENDING")
    } catch let e as E7Error {
      XCTAssertTrue(e.isPending, "\(e)")
    }
    let m = try await vm.loadAsync(code)
    XCTAssertEqual(try vm.call(m, "keySize"), .number(16))
    XCTAssertEqual(printed, ["init", "init"], "the abandoned load ran the top level once, loadAsync again")
  }
}
