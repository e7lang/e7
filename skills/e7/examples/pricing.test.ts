import { assertEqual, assertThrows } from "e7:test";
import { totalCents, parseCoupon, quote } from "./pricing.ts";

export function testTotalAddsTax(): void {
  assertEqual(totalCents([199, 301], 0.1), 550);
}

export function testCouponIsCaseInsensitive(): void {
  assertEqual(parseCoupon(" welcome10 "), 10);
}

export function testUnknownCouponIsRejected(): void {
  assertThrows(() => { parseCoupon("FREE"); }, "unknown coupon");
}

export function testQuote(): void {
  assertEqual(quote('{"items":[{"sku":"pen","cents":100,"qty":2}]}'), '{"total":220,"discount":0}');
}
