interface Item { sku: string; qty: number; unitPriceCents: number }
interface Order { id: string; region: string; customer: string; status: string; items: Item[]; discountPercent?: number }

interface RegionRow { region: string; orders: number; units: number; revenueCents: number; customers: number; topSku: string }
interface Report { regions: RegionRow[]; totalRevenueCents: number; skipped: number }

class RegionTotals {
  orders = 0;
  units = 0;
  revenueCents = 0;
  customers = new Set<string>();
  skuUnits = new Map<string, number>();

  constructor(readonly region: string) {}

  add(order: Order): void {
    this.orders++;
    this.customers.add(order.customer);
    let subtotal = 0;
    for (const item of order.items) {
      this.units += item.qty;
      subtotal += item.qty * item.unitPriceCents;
      this.skuUnits.set(item.sku, (this.skuUnits.get(item.sku) ?? 0) + item.qty);
    }
    this.revenueCents += Math.round((subtotal * (100 - (order.discountPercent ?? 0))) / 100);
  }

  topSku(): string {
    let best = "";
    let bestUnits = 0;
    for (const [sku, units] of this.skuUnits) {
      if (units > bestUnits || (units === bestUnits && units > 0 && sku < best)) {
        best = sku;
        bestUnits = units;
      }
    }
    return best;
  }

  row(): RegionRow {
    return {
      region: this.region, orders: this.orders, units: this.units, revenueCents: this.revenueCents,
      customers: this.customers.size, topSku: this.topSku(),
    };
  }
}

export function salesReport(ordersJson: string): string {
  let orders: Order[];
  try {
    orders = JSON.parse(ordersJson) as Order[];
  } catch (e) {
    if (e instanceof SyntaxError) return JSON.stringify({ error: "invalid json" });
    throw e;
  }
  const byRegion = new Map<string, RegionTotals>();
  let skipped = 0;
  for (const order of orders) {
    if (order.status !== "paid") {
      skipped++;
      continue;
    }
    let totals = byRegion.get(order.region);
    if (totals === undefined) {
      totals = new RegionTotals(order.region);
      byRegion.set(order.region, totals);
    }
    totals.add(order);
  }
  const regions = [...byRegion.values()].map((t) => t.row());
  regions.sort((a, b) => b.revenueCents - a.revenueCents || (a.region < b.region ? -1 : a.region > b.region ? 1 : 0));
  const report: Report = {
    regions,
    totalRevenueCents: regions.reduce((sum, r) => sum + r.revenueCents, 0),
    skipped,
  };
  return JSON.stringify(report);
}
