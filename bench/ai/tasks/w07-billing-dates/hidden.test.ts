import { assertEqual, assertThrows } from "e7:test";
import { addBusinessDays, billingDate, billingPeriod, proratedCents } from "./solution.ts";

const xmas = ["2024-12-24", "2024-12-25", "2024-12-26", "2025-01-01"];

export function testForwardOverWeekends(): void {
  assertEqual(addBusinessDays("2024-03-04", 4, []), "2024-03-08"); // Mon + 4 = Fri
  assertEqual(addBusinessDays("2024-03-04", 5, []), "2024-03-11");
  assertEqual(addBusinessDays("2024-03-09", 1, []), "2024-03-11"); // from Saturday
  assertEqual(addBusinessDays("2024-03-04", 20, []), "2024-04-01");
}

export function testForwardWithHolidays(): void {
  assertEqual(addBusinessDays("2024-12-20", 3, xmas), "2024-12-30");
  assertEqual(addBusinessDays("2024-12-23", 2, xmas), "2024-12-30");
  assertEqual(addBusinessDays("2024-12-30", 2, xmas), "2025-01-02");
}

export function testBackward(): void {
  assertEqual(addBusinessDays("2024-03-11", -1, []), "2024-03-08");
  assertEqual(addBusinessDays("2024-03-10", -1, []), "2024-03-08"); // from Sunday
  assertEqual(addBusinessDays("2024-12-27", -1, xmas), "2024-12-23");
  assertEqual(addBusinessDays("2025-01-02", -3, xmas), "2024-12-27");
}

export function testZeroDays(): void {
  assertEqual(addBusinessDays("2024-03-06", 0, []), "2024-03-06");
  assertEqual(addBusinessDays("2024-03-09", 0, []), "2024-03-11");
  assertEqual(addBusinessDays("2024-12-24", 0, xmas), "2024-12-27");
}

export function testAcrossYearsAndLeapDay(): void {
  assertEqual(addBusinessDays("2024-02-28", 1, []), "2024-02-29");
  assertEqual(addBusinessDays("2023-02-28", 1, []), "2023-03-01");
  assertEqual(addBusinessDays("2023-12-29", 1, []), "2024-01-01");
  assertEqual(addBusinessDays("2024-01-01", 260, []), "2024-12-30");
}

export function testBillingDatesFromMonthEnd(): void {
  const a = "2024-01-31";
  assertEqual([0, 1, 2, 3, 4, 13].map((c) => billingDate(a, c)),
    ["2024-01-31", "2024-02-29", "2024-03-31", "2024-04-30", "2024-05-31", "2025-02-28"]);
}

export function testBillingDatesOrdinaryAndNegative(): void {
  assertEqual(billingDate("2023-11-15", 2), "2024-01-15");
  assertEqual(billingDate("2023-11-15", 14), "2025-01-15");
  assertEqual(billingDate("2024-03-31", -1), "2024-02-29");
  assertEqual(billingDate("2024-03-30", -13), "2023-02-28");
  assertEqual(billingDate("2024-02-29", 12), "2025-02-28");
  assertEqual(billingDate("2024-02-29", 48), "2028-02-29");
}

export function testBillingPeriods(): void {
  assertEqual(billingPeriod("2024-01-31", 0), { start: "2024-01-31", end: "2024-02-28" });
  assertEqual(billingPeriod("2024-01-31", 2), { start: "2024-03-31", end: "2024-04-29" });
  assertEqual(billingPeriod("2024-01-01", 11), { start: "2024-12-01", end: "2024-12-31" });
  assertEqual(billingPeriod("2023-01-30", 0), { start: "2023-01-30", end: "2023-02-27" });
  assertEqual(billingPeriod("2023-01-30", 1), { start: "2023-02-28", end: "2023-03-29" });
}

export function testProration(): void {
  const p = { start: "2024-02-01", end: "2024-02-29" }; // 29 days
  assertEqual(proratedCents(999, p, "2024-02-01"), 999);
  assertEqual(proratedCents(999, p, "2024-02-29"), 34); // 999 / 29 = 34.45
  assertEqual(proratedCents(1000, p, "2024-02-15"), 517); // 15 / 29
  assertEqual(proratedCents(2900, p, "2024-02-15"), 1500);
  assertEqual(proratedCents(100, { start: "2024-03-01", end: "2024-03-04" }, "2024-03-03"), 50);
  assertEqual(proratedCents(5, { start: "2024-03-01", end: "2024-03-02" }, "2024-03-02"), 3); // 2.5 rounds up
}

export function testProrationOutsidePeriod(): void {
  const p = { start: "2024-02-01", end: "2024-02-29" };
  assertThrows(() => { proratedCents(100, p, "2024-01-31"); }, "date outside period: 2024-01-31");
  assertThrows(() => { proratedCents(100, p, "2024-03-01"); }, "date outside period: 2024-03-01");
}

export function testInvalidDates(): void {
  assertThrows(() => { addBusinessDays("2023-02-29", 1, []); }, "invalid date: 2023-02-29");
  assertThrows(() => { addBusinessDays("2024-1-05", 1, []); }, "invalid date: 2024-1-05");
  assertThrows(() => { billingDate("2024-13-01", 1); }, "invalid date: 2024-13-01");
  assertThrows(() => { billingDate("2024-04-31", 1); }, "invalid date: 2024-04-31");
  assertThrows(() => { billingPeriod("20240101", 0); }, "invalid date: 20240101");
  assertThrows(() => { billingPeriod("2024-01-01T00:00:00Z", 0); }, "invalid date: 2024-01-01T00:00:00Z");
  assertThrows(() => { proratedCents(1, { start: "2024-02-01", end: "2024-02-30" }, "2024-02-02"); }, "invalid date: 2024-02-30");
  assertThrows(() => { proratedCents(1, { start: "2024-02-01", end: "2024-02-28" }, "2024-2-02"); }, "invalid date: 2024-2-02");
  assertThrows(() => { addBusinessDays("2024-00-10", 1, []); }, "invalid date: 2024-00-10");
  assertThrows(() => { addBusinessDays("abcd-ef-gh", 1, []); }, "invalid date: abcd-ef-gh");
}
