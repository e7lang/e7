// E7 port of original.ts. All amounts are in cents.
import { tryParse } from "e7:json";

// One interface for the three rule kinds (E7 has no unions of object types).
interface DiscountRule {
  kind: string; // "percent" | "fixed" | "bogo"
  percent?: number;
  cents?: number;
  sku?: string;
}

interface CartLine {
  sku: string;
  unitCents: number;
  qty: number;
  tags?: string[];
}

interface Cart {
  lines: CartLine[];
  rules: DiscountRule[];
}

interface Totals {
  subtotal: number;
  discount: number;
  tax: number;
  total: number;
  taxFreeSkus: string[];
}

const TAX_RATES = new Map<string, number>([
  ["CA", 0.0725],
  ["NY", 0.04],
  ["TX", 0.0625],
]);

function isTaxFree(line: CartLine): boolean {
  return line.tags?.includes("food") ?? false;
}

function lineTotal(line: CartLine): number {
  if (!Number.isInteger(line.qty) || line.qty <= 0) {
    throw new Error(`bad quantity for ${line.sku}`);
  }
  return line.unitCents * line.qty;
}

function ruleDiscount(cart: Cart, subtotal: number, rule: DiscountRule): number {
  switch (rule.kind) {
    case "percent":
      return Math.round((subtotal * (rule.percent ?? 0)) / 100);
    case "fixed":
      return rule.cents ?? 0;
    case "bogo": {
      const line = cart.lines.find((l) => l.sku === rule.sku);
      return line ? Math.floor(line.qty / 2) * line.unitCents : 0;
    }
  }
  return 0;
}

function checkout(cart: Cart, state: string): Totals {
  const subtotal = cart.lines.map((l) => lineTotal(l)).reduce((a, b) => a + b, 0);
  let discount = 0;
  for (const rule of cart.rules) discount += ruleDiscount(cart, subtotal, rule);
  discount = Math.min(discount, subtotal);

  const rate = TAX_RATES.get(state.toUpperCase()) ?? 0;
  const taxable = cart.lines.filter((l) => !isTaxFree(l)).map((l) => lineTotal(l)).reduce((a, b) => a + b, 0);
  const taxableAfterDiscount = subtotal === 0 ? 0 : taxable - (discount * taxable) / subtotal;
  const tax = Math.round(taxableAfterDiscount * rate);

  const taxFreeSkus = [...new Set(cart.lines.filter((l) => isTaxFree(l)).map((l) => l.sku))].sort();
  return { subtotal, discount, tax, total: subtotal - discount + tax, taxFreeSkus };
}

interface ErrorResult { error: string }

export function checkoutJson(cartJson: string, state: string): string {
  const cart = tryParse(cartJson) as Cart | null;
  if (cart === null) {
    const e: ErrorResult = { error: "invalid json" };
    return JSON.stringify(e);
  }
  try {
    return JSON.stringify(checkout(cart, state));
  } catch (e) {
    const r: ErrorResult = { error: e instanceof Error ? e.message : String(e) };
    return JSON.stringify(r);
  }
}
