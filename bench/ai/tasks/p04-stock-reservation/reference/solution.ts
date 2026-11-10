// E7 port of original.ts: stock reservation for incoming orders.
import { tryParse } from "e7:json";

interface StockRecord {
  onHand: number;
  reserved: number;
  safety?: number;
  backorderable?: boolean;
  leadDays?: number;
}

interface OrderLine {
  sku: string;
  qty: number;
}

interface ReservationRequest {
  orderId: string;
  mode: string; // "all-or-nothing" | "partial"; a string so an unknown mode gets its own error
  lines: OrderLine[];
}

// One interface for the four result kinds; fields a kind does not have stay unset (left out of the JSON).
interface LineResult {
  status: string; // "reserved" | "split" | "backordered" | "rejected"
  sku: string;
  qty?: number;
  reserved?: number;
  backordered?: number;
  etaDays?: number;
  reason?: string;
}

interface Availability {
  get free(): number;
  get canBackorder(): boolean;
  get etaDays(): number;
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

function planLine(sku: string, qty: number, item: Availability): LineResult {
  const free = item.free;
  if (qty <= free) return { status: "reserved", sku, qty };
  if (item.canBackorder) {
    return free > 0
      ? { status: "split", sku, reserved: free, backordered: qty - free, etaDays: item.etaDays }
      : { status: "backordered", sku, qty, etaDays: item.etaDays };
  }
  return { status: "rejected", sku, reason: `insufficient stock (${free} available)` };
}

function reservedQty(r: LineResult): number {
  return r.status === "reserved" ? (r.qty ?? 0) : r.status === "split" ? (r.reserved ?? 0) : 0;
}

interface ReservationResult {
  orderId: string;
  committed: boolean;
  lines: LineResult[];
  remaining: [string, number][];
}

function reserve(stock: Map<string, StockRecord>, req: ReservationRequest): ReservationResult {
  if (!req.orderId) throw new Error("missing orderId");
  if (req.mode !== "all-or-nothing" && req.mode !== "partial") throw new Error(`unknown mode "${req.mode}"`);

  // the same SKU may appear on several lines; lines of 0 are ignored
  const wanted = new Map<string, number>();
  for (const line of req.lines) wanted.set(line.sku, (wanted.get(line.sku) ?? 0) + line.qty);
  for (const [sku, qty] of [...wanted]) if (qty === 0) wanted.delete(sku);

  const results: LineResult[] = [];
  const items = new Map<string, StockItem>();
  for (const [sku, qty] of wanted) {
    const rec = stock.get(sku);
    if (rec === undefined) {
      results.push({ status: "rejected", sku, reason: "unknown sku" });
    } else if (!Number.isInteger(qty) || qty < 0) {
      results.push({ status: "rejected", sku, reason: "invalid quantity" });
    } else {
      const item = new StockItem(rec);
      items.set(sku, item);
      results.push(planLine(sku, qty, item));
    }
  }

  const complete = results.every((r) => r.status === "reserved");
  const commit = req.mode === "partial" ? results.some((r) => reservedQty(r) > 0) : complete;
  if (commit) {
    for (const r of results) items.get(r.sku)?.reserve(reservedQty(r));
  }

  const remaining: [string, number][] = [];
  for (const [sku, item] of items) remaining.push([sku, item.free]);
  return { orderId: req.orderId, committed: commit, lines: results, remaining };
}

interface ErrorResult {
  error: string;
}

function errorJson(message: string): string {
  const e: ErrorResult = { error: message };
  return JSON.stringify(e);
}

// Called by the app with the stock table and the request as JSON.
export function reserveStock(stockJson: string, requestJson: string): string {
  const stock = tryParse(stockJson) as Map<string, StockRecord> | null;
  const req = tryParse(requestJson) as ReservationRequest | null;
  if (stock === null || req === null) return errorJson("invalid json");
  try {
    return JSON.stringify(reserve(stock, req));
  } catch (e) {
    return errorJson(e instanceof Error ? e.message : String(e));
  }
}
