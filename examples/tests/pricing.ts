// A small module under test (see pricing.test.ts): order totals with discount codes.
export interface Line { sku: string; unitCents: number; qty: number }
export interface Total { subtotal: number; discount: number; total: number }

const codes = new Map<string, number>([["WELCOME10", 10], ["VIP25", 25]]);

export function discountPercent(code: string): number {
  const p = codes.get(code.trim().toUpperCase());
  if (p === undefined) throw new Error(`unknown discount code '${code}'`);
  return p;
}

export function totalOf(lines: Line[], code: string | null): Total {
  let subtotal = 0;
  for (const l of lines) {
    if (l.qty <= 0) throw new Error(`invalid quantity for ${l.sku}`);
    subtotal += l.unitCents * l.qty;
  }
  const discount = code === null ? 0 : Math.round((subtotal * discountPercent(code)) / 100);
  return { subtotal, discount, total: subtotal - discount };
}

export function formatCents(c: number): string {
  return `$${Math.floor(c / 100)}.${String(c % 100).padStart(2, "0")}`;
}
