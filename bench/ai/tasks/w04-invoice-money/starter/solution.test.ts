import { assertEqual } from "e7:test";
import { formatMoney, invoiceTotals, lineTaxCents } from "./solution.ts";

export function testFormat(): void {
  assertEqual(formatMoney(123456789, "USD"), "$1,234,567.89");
  assertEqual(formatMoney(-500, "EUR"), "-5,00 €");
}

export function testHalfRounding(): void {
  const item = { sku: "gum", unitCents: 25, quantity: 1, taxRateBp: 1000 }; // tax 2.5 cents
  assertEqual(lineTaxCents(item, "half-up"), 3);
  assertEqual(lineTaxCents(item, "half-even"), 2);
}

export function testInvoice(): void {
  const items = [{ sku: "mug", unitCents: 1999, quantity: 3, taxRateBp: 825 }];
  assertEqual(invoiceTotals(items, "USD", "half-even"), {
    subtotalCents: 5997, taxCents: 495, totalCents: 6492,
    subtotal: "$59.97", tax: "$4.95", total: "$64.92",
  });
}
