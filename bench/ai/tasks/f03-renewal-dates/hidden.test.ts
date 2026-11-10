import { assertEqual, assertThrows } from "e7:test";
import { renewalDates, refundCents } from "./solution.ts";

export function testEndOfMonthLeapYear(): void {
  assertEqual(renewalDates("2024-01-31", 1, 4), ["2024-02-29", "2024-03-31", "2024-04-30", "2024-05-31"]);
}

export function testEndOfMonthCommonYear(): void {
  assertEqual(renewalDates("2023-01-31", 1, 3), ["2023-02-28", "2023-03-31", "2023-04-30"]);
}

export function testAcrossYearEnd(): void {
  assertEqual(renewalDates("2024-12-31", 1, 3), ["2025-01-31", "2025-02-28", "2025-03-31"]);
}

export function testYearlyFromLeapDay(): void {
  assertEqual(renewalDates("2024-02-29", 12, 4), ["2025-02-28", "2026-02-28", "2027-02-28", "2028-02-29"]);
}

export function testQuarterlyOnThe30th(): void {
  assertEqual(renewalDates("2023-11-30", 3, 4), ["2024-02-29", "2024-05-30", "2024-08-30", "2024-11-30"]);
}

export function testMidMonthHalfYearly(): void {
  assertEqual(renewalDates("2024-06-15", 6, 3), ["2024-12-15", "2025-06-15", "2025-12-15"]);
}

export function testZeroCount(): void {
  const none: string[] = [];
  assertEqual(renewalDates("2024-01-31", 1, 0), none);
}

export function testInvalidInput(): void {
  assertThrows(() => { renewalDates("2024-02-30", 1, 1); }, "invalid date 2024-02-30");
  assertThrows(() => { renewalDates("2024-2-03", 1, 1); }, "invalid date 2024-2-03");
  assertThrows(() => { renewalDates("2024-01-31", 0, 1); }, "invalid period 0");
  assertThrows(() => { refundCents(100, "2024-01-31", 1.5, "2024-02-01"); }, "invalid period 1.5");
}

export function testRefundAfterShortMonth(): void {
  // current period 2024-02-29 .. 2024-03-31 (31 days), 16 days unused
  assertEqual(refundCents(3100, "2024-01-31", 1, "2024-03-15"), 1600);
}

export function testRefundLastDayOfPeriod(): void {
  assertEqual(refundCents(2900, "2024-01-31", 1, "2024-03-30"), 94);
}

export function testRefundOnStartAndRenewalDay(): void {
  assertEqual(refundCents(999, "2024-01-31", 1, "2024-01-31"), 999);
  assertEqual(refundCents(999, "2024-01-31", 1, "2024-02-29"), 999);
}

export function testRefundYearly(): void {
  assertEqual(refundCents(36600, "2024-02-29", 12, "2025-03-01"), 36500);
}

export function testCancelBeforeStart(): void {
  assertThrows(() => { refundCents(999, "2024-01-31", 1, "2024-01-30"); }, "cancel date before start");
}
