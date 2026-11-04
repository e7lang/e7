// Node.js implementation of the e7:test module (same semantics as the e7vm natives).
import { inspect } from "node:util";

const show = (v) => inspect(v, { depth: 4, breakLength: Infinity });
const head = (what, msg) => what + " failed" + (msg ? ": " + msg : "");

function deepEq(a, b) {
  if (a === b) return true;
  if (typeof a === "number" && typeof b === "number") return a !== a && b !== b; // NaN equals NaN
  if (typeof a !== "object" || typeof b !== "object" || a === null || b === null) return false;
  if (Object.getPrototypeOf(a) !== Object.getPrototypeOf(b)) return false;
  if (a instanceof Date) return a.getTime() === b.getTime() || (a.getTime() !== a.getTime() && b.getTime() !== b.getTime());
  if (a instanceof Uint8Array) return a.length === b.length && a.every((x, i) => x === b[i]);
  if (Array.isArray(a)) return a.length === b.length && a.every((x, i) => deepEq(x, b[i]));
  if (a instanceof Map) {
    if (a.size !== b.size) return false;
    for (const [k, v] of a) if (!b.has(k) || !deepEq(v, b.get(k))) return false;
    return true;
  }
  if (a instanceof Set) {
    if (a.size !== b.size) return false;
    for (const k of a) if (!b.has(k)) return false;
    return true;
  }
  const ka = Object.keys(a), kb = Object.keys(b);
  return ka.length === kb.length && ka.every((k) => Object.prototype.hasOwnProperty.call(b, k) && deepEq(a[k], b[k]));
}

export function assert(cond, message) {
  if (!cond) throw new Error(message ? "assertion failed: " + message : "assertion failed");
}
export function fail(message) {
  throw new Error(message ? "test failed: " + message : "test failed");
}
export function assertEqual(actual, expected, message) {
  if (!deepEq(actual, expected))
    throw new Error(head("assertEqual", message) + "\n  expected: " + show(expected) + "\n  actual:   " + show(actual));
}
export function assertNotEqual(actual, expected, message) {
  if (deepEq(actual, expected)) throw new Error(head("assertNotEqual", message) + "\n  both values: " + show(actual));
}
export function assertClose(actual, expected, tolerance = 1e-9, message) {
  if (!(Math.abs(actual - expected) <= tolerance || actual === expected))
    throw new Error(head("assertClose", message) + "\n  expected: " + expected + " (tolerance " + tolerance + ")\n  actual:   " + actual);
}
export function assertThrows(fn, expectedMessage) {
  try {
    fn();
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    if (expectedMessage && !msg.includes(expectedMessage))
      throw new Error("assertThrows failed\n  expected an error containing: " + expectedMessage + "\n  actual error: " + msg);
    return;
  }
  throw new Error("assertThrows failed: the function did not throw");
}
