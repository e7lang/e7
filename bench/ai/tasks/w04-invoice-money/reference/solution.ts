export type Rounding = "half-up" | "half-even";
export interface LineItem { sku: string; unitCents: number; quantity: number; taxRateBp: number }
export interface InvoiceTotals { subtotalCents: number; taxCents: number; totalCents: number; subtotal: string; tax: string; total: string }

// Rounds numerator / denominator (integers, denominator > 0) to an integer.
function roundRatio(numerator: number, denominator: number, mode: Rounding): number {
  const q = Math.trunc(numerator / denominator);
  const twiceRem = Math.abs(numerator - q * denominator) * 2;
  const sign = numerator < 0 ? -1 : 1;
  if (twiceRem > denominator) return q + sign;
  if (twiceRem < denominator) return q;
  if (mode === "half-up") return q + sign;
  return q % 2 === 0 ? q : q + sign;
}

function groupDigits(digits: string, sep: string): string {
  let out = "";
  for (let i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 === 0) out += sep;
    out += digits[i];
  }
  return out;
}

export function formatMoney(cents: number, currency: string): string {
  const abs = Math.abs(cents);
  const minus = cents < 0 ? "-" : "";
  const units = String(Math.floor(abs / 100));
  const frac = String(abs % 100).padStart(2, "0");
  switch (currency) {
    case "USD": return `${minus}$${groupDigits(units, ",")}.${frac}`;
    case "GBP": return `${minus}£${groupDigits(units, ",")}.${frac}`;
    case "EUR": return `${minus}${groupDigits(units, ".")},${frac} €`;
    case "CHF": return `CHF ${minus}${groupDigits(units, "'")}.${frac}`;
  }
  throw new Error(`unsupported currency: ${currency}`);
}

export function lineTaxCents(item: LineItem, mode: Rounding): number {
  return roundRatio(item.unitCents * item.quantity * item.taxRateBp, 10000, mode);
}

export function invoiceTotals(items: LineItem[], currency: string, mode: Rounding): InvoiceTotals {
  let subtotalCents = 0;
  let taxCents = 0;
  for (const item of items) {
    if (!Number.isInteger(item.quantity) || item.quantity <= 0) throw new Error(`invalid quantity for ${item.sku}`);
    subtotalCents += item.unitCents * item.quantity;
    taxCents += lineTaxCents(item, mode);
  }
  const totalCents = subtotalCents + taxCents;
  return {
    subtotalCents, taxCents, totalCents,
    subtotal: formatMoney(subtotalCents, currency),
    tax: formatMoney(taxCents, currency),
    total: formatMoney(totalCents, currency),
  };
}
