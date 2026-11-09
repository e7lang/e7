import { assertEqual } from "e7:test";
import { addBusinessDays, billingDate, billingPeriod, proratedCents } from "./solution.ts";

export function testBusinessDays(): void {
  // 2024-03-08 is a Friday
  assertEqual(addBusinessDays("2024-03-08", 1, []), "2024-03-11");
  assertEqual(addBusinessDays("2024-03-08", 1, ["2024-03-11"]), "2024-03-12");
}

export function testMonthEndAnchor(): void {
  assertEqual(billingDate("2024-01-31", 1), "2024-02-29");
  assertEqual(billingPeriod("2024-01-31", 1), { start: "2024-02-29", end: "2024-03-30" });
}

export function testProration(): void {
  // 30-day period, 10 days left
  assertEqual(proratedCents(3000, { start: "2024-04-01", end: "2024-04-30" }, "2024-04-21"), 1000);
}
