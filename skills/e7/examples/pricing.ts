// Price rules that change faster than app releases: typed input, validation that throws on
// contract violations, and a JSON result for the host.
export function totalCents(itemsCents: number[], taxRate: number): number {
  const subtotal = itemsCents.reduce((sum, c) => sum + c, 0);
  return Math.round(subtotal * (1 + taxRate));
}

const COUPONS = new Map<string, number>([["WELCOME10", 10], ["VIP20", 20]]);

export function parseCoupon(code: string): number {
  const pct = COUPONS.get(code.trim().toUpperCase());
  if (pct === undefined) throw new Error(`unknown coupon: ${code}`);
  return pct;
}

interface Cart { items: { sku: string; cents: number; qty: i32 }[]; coupon?: string }

export function quote(cartJson: string): string {
  const cart = JSON.parse(cartJson) as Cart;
  const lines = cart.items.map(it => it.cents * Number(it.qty));
  const discount = cart.coupon !== undefined ? parseCoupon(cart.coupon) : 0;
  const total = Math.round(totalCents(lines, 0.1) * (100 - discount) / 100);
  return JSON.stringify({ total, discount });
}

export function main(): string {
  return quote('{"items":[{"sku":"pen","cents":199,"qty":3}],"coupon":"welcome10"}');
}
