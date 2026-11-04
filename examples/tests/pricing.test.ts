// Run with:  e7 test examples/tests
// Every exported function whose name starts with "test" runs in a fresh VM.
import { assert, assertEqual, assertThrows } from "e7:test";
import { totalOf, discountPercent, formatCents } from "./pricing.ts";
import type { Line } from "./pricing.ts";

const cart: Line[] = [
  { sku: "book", unitCents: 1250, qty: 2 },
  { sku: "pen", unitCents: 199, qty: 3 },
];

export function testSubtotalWithoutCode(): void {
  assertEqual(totalOf(cart, null), { subtotal: 3097, discount: 0, total: 3097 });
}

export function testDiscountCodesAreCaseInsensitive(): void {
  assertEqual(discountPercent(" welcome10 "), 10);
  assertEqual(totalOf(cart, "vip25").total, 2323);
}

export function testUnknownCodeIsRejected(): void {
  assertThrows(() => { discountPercent("FREE"); }, "unknown discount code");
}

export function testInvalidQuantity(): void {
  assertThrows(() => { totalOf([{ sku: "x", unitCents: 1, qty: 0 }], null); }, "invalid quantity");
}

// a test may also return a boolean: false fails it
export function testFormatting(): boolean {
  return formatCents(3097) === "$30.97" && formatCents(5) === "$0.05";
}

export function testEmptyCart(): void {
  const t = totalOf([], "VIP25");
  assert(t.total === 0, "an empty cart costs nothing");
}
