import { assertEqual, assertThrows } from "e7:test";
import { formatMoney, invoiceTotals, lineTaxCents } from "./solution.ts";
import type { LineItem } from "./solution.ts";

function item(unitCents: number, quantity: number, taxRateBp: number): LineItem {
  return { sku: "x", unitCents, quantity, taxRateBp };
}

export function testFormatAllCurrencies(): void {
  assertEqual(formatMoney(123456789, "GBP"), "£1,234,567.89");
  assertEqual(formatMoney(123456789, "EUR"), "1.234.567,89 €");
  assertEqual(formatMoney(123456789, "CHF"), "CHF 1'234'567.89");
  assertEqual(formatMoney(-500, "USD"), "-$5.00");
  assertEqual(formatMoney(-500, "GBP"), "-£5.00");
  assertEqual(formatMoney(-500, "CHF"), "CHF -5.00");
}

export function testFormatSmallAndGrouping(): void {
  assertEqual(formatMoney(0, "USD"), "$0.00");
  assertEqual(formatMoney(7, "USD"), "$0.07");
  assertEqual(formatMoney(-7, "EUR"), "-0,07 €");
  assertEqual(formatMoney(99999, "USD"), "$999.99");
  assertEqual(formatMoney(100000, "USD"), "$1,000.00");
  assertEqual(formatMoney(100000000, "EUR"), "1.000.000,00 €");
  assertEqual(formatMoney(12345678901234, "CHF"), "CHF 123'456'789'012.34");
}

export function testUnsupportedCurrency(): void {
  assertThrows(() => { formatMoney(100, "JPY"); }, "unsupported currency: JPY");
  assertThrows(() => { invoiceTotals([], "usd", "half-up"); }, "unsupported currency: usd");
}

export function testHalvesBothModes(): void {
  // 5 cents at 10% = 0.5, 15 -> 1.5, 25 -> 2.5, 35 -> 3.5
  assertEqual([5, 15, 25, 35].map((c) => lineTaxCents(item(c, 1, 1000), "half-up")), [1, 2, 3, 4]);
  assertEqual([5, 15, 25, 35].map((c) => lineTaxCents(item(c, 1, 1000), "half-even")), [0, 2, 2, 4]);
}

export function testNegativeHalves(): void {
  assertEqual(lineTaxCents(item(-25, 1, 1000), "half-up"), -3);
  assertEqual(lineTaxCents(item(-25, 1, 1000), "half-even"), -2);
  assertEqual(lineTaxCents(item(-35, 1, 1000), "half-even"), -4);
  assertEqual(lineTaxCents(item(-5, 1, 1000), "half-up"), -1);
}

export function testNonHalvesRoundToNearest(): void {
  // 1999 * 3 * 8.25% = 494.7525
  assertEqual(lineTaxCents(item(1999, 3, 825), "half-up"), 495);
  // 101 * 7% = 7.07
  assertEqual(lineTaxCents(item(101, 1, 700), "half-even"), 7);
  // 1005 * 0.5% = 5.025 -> 5, -1005 * 0.5% = -5.025 -> -5
  assertEqual(lineTaxCents(item(1005, 1, 50), "half-up"), 5);
  assertEqual(lineTaxCents(item(-1005, 1, 50), "half-up"), -5);
  // 333 * 3 * 7.5% = 74.925 -> 75
  assertEqual(lineTaxCents(item(333, 3, 750), "half-even"), 75);
  assertEqual(lineTaxCents(item(-333, 3, 750), "half-even"), -75);
}

export function testExactWhereFloatsAreNot(): void {
  // 1.005 * 100 style traps: 201 cents at 2.5% = 5.025 -> 5; 1005 at 1% = 10.05 -> 10
  assertEqual(lineTaxCents(item(201, 1, 250), "half-up"), 5);
  assertEqual(lineTaxCents(item(1005, 1, 100), "half-up"), 10);
  // 2.675 cents exactly: 107 * 2.5% = 2.675 -> 3
  assertEqual(lineTaxCents(item(107, 1, 250), "half-even"), 3);
  // 0.125 * 10: 125 at 10% = 12.5 -> 13 / 12
  assertEqual(lineTaxCents(item(125, 1, 1000), "half-up"), 13);
  assertEqual(lineTaxCents(item(125, 1, 1000), "half-even"), 12);
}

export function testZeroRate(): void {
  assertEqual(lineTaxCents(item(999, 4, 0), "half-up"), 0);
}

export function testPerLineRounding(): void {
  // each line has 0.5 cent of tax: per-line rounding gives 1 + 1 (half-up), not round(1.0) = 1
  const items = [item(5, 1, 1000), item(5, 1, 1000)];
  assertEqual(invoiceTotals(items, "USD", "half-up").taxCents, 2);
  assertEqual(invoiceTotals(items, "USD", "half-even").taxCents, 0);
}

export function testInvoiceWithCreditLine(): void {
  const items: LineItem[] = [
    { sku: "laptop", unitCents: 129900, quantity: 2, taxRateBp: 1900 },
    { sku: "case", unitCents: 2450, quantity: 1, taxRateBp: 1900 },
    { sku: "voucher", unitCents: -5000, quantity: 1, taxRateBp: 1900 },
    { sku: "book", unitCents: 1250, quantity: 3, taxRateBp: 700 },
  ];
  // taxes: 49362, 465.5 -> 466/466, -950, 262.5 -> 263/262
  assertEqual(invoiceTotals(items, "EUR", "half-up"), {
    subtotalCents: 261000, taxCents: 49141, totalCents: 310141,
    subtotal: "2.610,00 €", tax: "491,41 €", total: "3.101,41 €",
  });
  assertEqual(invoiceTotals(items, "EUR", "half-even").taxCents, 49140);
}

export function testEmptyInvoice(): void {
  assertEqual(invoiceTotals([], "GBP", "half-even"), {
    subtotalCents: 0, taxCents: 0, totalCents: 0, subtotal: "£0.00", tax: "£0.00", total: "£0.00",
  });
}

export function testNegativeTotal(): void {
  const r = invoiceTotals([item(-1000, 1, 2000)], "CHF", "half-up");
  assertEqual(r.totalCents, -1200);
  assertEqual(r.total, "CHF -12.00");
}

export function testInvalidQuantity(): void {
  assertThrows(() => { invoiceTotals([{ sku: "pen", unitCents: 100, quantity: 0, taxRateBp: 0 }], "USD", "half-up"); }, "invalid quantity for pen");
  assertThrows(() => { invoiceTotals([{ sku: "ink", unitCents: 100, quantity: 1.5, taxRateBp: 0 }], "USD", "half-up"); }, "invalid quantity for ink");
  assertThrows(() => { invoiceTotals([{ sku: "pad", unitCents: 100, quantity: -2, taxRateBp: 0 }], "USD", "half-up"); }, "invalid quantity for pad");
}
