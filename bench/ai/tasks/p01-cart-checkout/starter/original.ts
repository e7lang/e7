// Checkout totals for the web shop. All amounts are in cents.

type Cents = number;

export type DiscountRule =
  | { kind: "percent"; percent: number }
  | { kind: "fixed"; cents: Cents }
  | { kind: "bogo"; sku: string };

export interface CartLine {
  sku: string;
  unitCents: Cents;
  qty: number;
  tags?: string[];
}

export interface Cart {
  lines: CartLine[];
  rules: DiscountRule[];
}

export interface Totals {
  subtotal: Cents;
  discount: Cents;
  tax: Cents;
  total: Cents;
  taxFreeSkus: string[];
}

const TAX_RATES: Record<string, number> = {
  CA: 0.0725,
  NY: 0.04,
  TX: 0.0625,
};

const isTaxFree = (line: CartLine) => line.tags?.includes("food") ?? false;

function lineTotal(line: CartLine): Cents {
  if (!Number.isInteger(line.qty) || line.qty <= 0) {
    throw new Error(`bad quantity for ${line.sku}`);
  }
  return line.unitCents * line.qty;
}

function ruleDiscount(cart: Cart, subtotal: Cents, rule: DiscountRule): Cents {
  switch (rule.kind) {
    case "percent":
      return Math.round((subtotal * rule.percent) / 100);
    case "fixed":
      return rule.cents;
    case "bogo": {
      const line = cart.lines.find((l) => l.sku === rule.sku);
      return line ? Math.floor(line.qty / 2) * line.unitCents : 0;
    }
  }
}

export function checkout(cart: Cart, state: string): Totals {
  const subtotal = cart.lines.map(lineTotal).reduce((a, b) => a + b, 0);
  let discount = 0;
  for (const rule of cart.rules) discount += ruleDiscount(cart, subtotal, rule);
  discount = Math.min(discount, subtotal);

  const rate = TAX_RATES[state.toUpperCase()] ?? 0;
  const taxable = cart.lines.filter((l) => !isTaxFree(l)).map(lineTotal).reduce((a, b) => a + b, 0);
  // the discount is spread over taxable and tax-free lines in proportion to their value
  const taxableAfterDiscount = subtotal === 0 ? 0 : taxable - (discount * taxable) / subtotal;
  const tax = Math.round(taxableAfterDiscount * rate);

  const taxFreeSkus = [...new Set(cart.lines.filter(isTaxFree).map((l) => l.sku))].sort();
  return { subtotal, discount, tax, total: subtotal - discount + tax, taxFreeSkus };
}

export function checkoutJson(cartJson: string, state: string): string {
  let cart: Cart;
  try {
    cart = JSON.parse(cartJson);
  } catch {
    return JSON.stringify({ error: "invalid json" });
  }
  try {
    return JSON.stringify(checkout(cart, state));
  } catch (e) {
    return JSON.stringify({ error: (e as Error).message });
  }
}
