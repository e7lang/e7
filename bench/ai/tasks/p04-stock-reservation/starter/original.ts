// Stock reservation for incoming orders. Reserves from free stock (on hand minus reserved minus the safety
// buffer), backorders what may be backordered and rejects the rest.

export enum ReserveMode {
  AllOrNothing = "all-or-nothing",
  Partial = "partial",
}

export interface StockRecord {
  onHand: number;
  reserved: number;
  safety?: number;
  backorderable?: boolean;
  leadDays?: number;
}

type StockTable = { [sku: string]: StockRecord };

export interface OrderLine {
  sku: string;
  qty: number;
}

export interface ReservationRequest {
  orderId: string;
  mode: ReserveMode;
  lines: OrderLine[];
}

export type LineResult =
  | { status: "reserved"; sku: string; qty: number }
  | { status: "split"; sku: string; reserved: number; backordered: number; etaDays: number }
  | { status: "backordered"; sku: string; qty: number; etaDays: number }
  | { status: "rejected"; sku: string; reason: string };

interface Availability {
  readonly free: number;
  readonly canBackorder: boolean;
}

class StockItem implements Availability {
  constructor(private rec: StockRecord) {}
  get free(): number {
    return Math.max(0, this.rec.onHand - this.rec.reserved - (this.rec.safety ?? 0));
  }
  get canBackorder(): boolean {
    return this.rec.backorderable === true;
  }
  get etaDays(): number {
    return this.rec.leadDays ?? 14;
  }
  reserve(qty: number): void {
    this.rec.reserved += qty;
  }
}

function planLine(sku: string, qty: number, item: Availability & { etaDays: number }): LineResult {
  const free = item.free;
  if (qty <= free) return { status: "reserved", sku, qty };
  if (item.canBackorder) {
    return free > 0
      ? { status: "split", sku, reserved: free, backordered: qty - free, etaDays: item.etaDays }
      : { status: "backordered", sku, qty, etaDays: item.etaDays };
  }
  return { status: "rejected", sku, reason: `insufficient stock (${free} available)` };
}

const reservedQty = (r: LineResult): number =>
  r.status === "reserved" ? r.qty : r.status === "split" ? r.reserved : 0;

export function reserve(stock: StockTable, req: ReservationRequest) {
  if (!req.orderId) throw new Error("missing orderId");
  if (!Object.values(ReserveMode).includes(req.mode)) throw new Error(`unknown mode "${req.mode}"`);

  // the same SKU may appear on several lines; lines of 0 are ignored
  const wanted: Record<string, number> = {};
  for (const line of req.lines) wanted[line.sku] = (wanted[line.sku] ?? 0) + line.qty;
  for (const sku in wanted) if (wanted[sku] === 0) delete wanted[sku];

  const results: LineResult[] = [];
  const items: Record<string, StockItem> = {};
  for (const sku in wanted) {
    const qty = wanted[sku];
    if (!(sku in stock)) {
      results.push({ status: "rejected", sku, reason: "unknown sku" });
    } else if (!Number.isInteger(qty) || qty < 0) {
      results.push({ status: "rejected", sku, reason: "invalid quantity" });
    } else {
      items[sku] = new StockItem(stock[sku]);
      results.push(planLine(sku, qty, items[sku]));
    }
  }

  const complete = results.every((r) => r.status === "reserved");
  const commit = req.mode === ReserveMode.Partial ? results.some((r) => reservedQty(r) > 0) : complete;
  if (commit) {
    for (const r of results) if (r.sku in items) items[r.sku].reserve(reservedQty(r));
  }

  const remaining: Record<string, number> = {};
  for (const sku in items) remaining[sku] = items[sku].free;
  return { orderId: req.orderId, committed: commit, lines: results, remaining: Object.entries(remaining) };
}

// Called by the app with the stock table and the request as JSON.
export function reserveStock(stockJson: string, requestJson: string): string {
  let stock: StockTable;
  let req: ReservationRequest;
  try {
    stock = JSON.parse(stockJson);
    req = JSON.parse(requestJson);
  } catch {
    return JSON.stringify({ error: "invalid json" });
  }
  try {
    return JSON.stringify(reserve(stock, req));
  } catch (e) {
    return JSON.stringify({ error: (e as Error).message });
  }
}
