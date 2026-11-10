import { assertEqual } from "e7:test";
import { renewalDates, refundCents } from "./solution.ts";

export function testMonthly(): void {
  assertEqual(renewalDates("2024-01-15", 1, 3), ["2024-02-15", "2024-03-15", "2024-04-15"]);
}

export function testShortMonthUsesLastDay(): void {
  assertEqual(renewalDates("2024-03-31", 1, 1), ["2024-04-30"]);
}

export function testRefundHalfPeriod(): void {
  // period 2024-04-10 .. 2024-05-10 has 30 days, 15 of them unused
  assertEqual(refundCents(3000, "2024-04-10", 1, "2024-04-25"), 1500);
}
