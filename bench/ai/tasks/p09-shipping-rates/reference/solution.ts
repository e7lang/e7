// E7 port of original.ts: shipping quotes by zone, billable weight and surcharges. Amounts in euro cents.
import { tryParse } from "e7:json";

type Service = "economy" | "standard" | "express";
type Zone = "domestic" | "eu" | "europe" | "world";
type RateTable = [number, number][]; // [maxKg, cents], ascending by weight

const ZONE_BY_COUNTRY = new Map<string, Zone>([
  ["DE", "domestic"],
  ["AT", "eu"], ["FR", "eu"], ["NL", "eu"], ["IT", "eu"], ["ES", "eu"], ["DK", "eu"],
  ["CH", "europe"], ["GB", "europe"], ["NO", "europe"],
  ["US", "world"], ["CA", "world"], ["JP", "world"], ["AU", "world"],
]);

interface ServiceRates {
  service: Service;
  table: RateTable;
}

// services in the order of the original's tables
const RATES = new Map<Zone, ServiceRates[]>([
  ["domestic", [
    { service: "economy", table: [[1, 399], [5, 599], [10, 899], [31.5, 1499]] },
    { service: "standard", table: [[2, 549], [10, 799], [31.5, 1699]] },
    { service: "express", table: [[5, 1490], [20, 2490]] },
  ]],
  ["eu", [
    { service: "standard", table: [[2, 1299], [5, 1799], [10, 2499], [31.5, 3999]] },
    { service: "express", table: [[5, 3490], [20, 5990]] },
  ]],
  ["europe", [
    { service: "standard", table: [[2, 1599], [5, 2299], [20, 4499]] },
    { service: "express", table: [[5, 4490], [20, 7990]] },
  ]],
  ["world", [
    { service: "standard", table: [[2, 2999], [5, 4499], [10, 6999], [20, 9999]] },
  ]],
]);

const isDigit = (c: string) => c >= "0" && c <= "9";

// remote areas (the original's regular expressions; the postcode has no whitespace here)
function isRemote(country: string, postcode: string): boolean {
  switch (country) {
    case "DE":
      return ["18565", "25849", "25859", "25863", "25869", "27498"].some((p) => postcode.startsWith(p));
    case "GB": {
      const pc = postcode.toUpperCase();
      if (["BT", "HS", "KW", "ZE"].some((p) => pc.startsWith(p))) return true;
      return pc.startsWith("IV4") && pc.length > 3 && isDigit(pc[3]);
    }
    case "DK":
      return postcode.length === 4 && ["37", "38", "39"].some((p) => postcode.startsWith(p)) && isDigit(postcode[2]) && isDigit(postcode[3]);
  }
  return false;
}

function removeSpaces(s: string): string {
  let out = "";
  for (const c of s) if (c.trim() !== "") out += c;
  return out;
}

// One interface for the three surcharge kinds.
interface Surcharge {
  kind: string; // "flat" | "percent" | "perKg"
  label: string;
  cents?: number;
  percent?: number;
  centsPerKg?: number;
  aboveKg?: number;
}

interface Parcel {
  weightKg: number;
  lengthCm: number;
  widthCm: number;
  heightCm: number;
}

interface Billable {
  get billableKg(): number;
  get oversize(): boolean;
}

class ParcelInfo implements Billable {
  constructor(private p: Parcel) {}
  get volumetricKg(): number {
    return (this.p.lengthCm * this.p.widthCm * this.p.heightCm) / 5000;
  }
  // the larger of real and volumetric weight, rounded up to the next half kilo
  get billableKg(): number {
    return Math.ceil(Math.max(this.p.weightKg, this.volumetricKg) * 2) / 2;
  }
  get oversize(): boolean {
    const [a, b, c] = [this.p.lengthCm, this.p.widthCm, this.p.heightCm].sort((x, y) => y - x);
    return a > 120 || a + 2 * (b + c) > 300;
  }
}

function surchargesFor(parcel: Billable, service: Service, remote: boolean): Surcharge[] {
  const list: Surcharge[] = [];
  if (remote) list.push({ kind: "flat", label: "remote area", cents: 1500 });
  if (parcel.oversize) list.push({ kind: "flat", label: "oversize", cents: 2500 });
  if (service === "express") list.push({ kind: "percent", label: "fuel", percent: 8.5 });
  if (parcel.billableKg > 20) list.push({ kind: "perKg", label: "heavy", centsPerKg: 45, aboveKg: 20 });
  return list;
}

function surchargeCents(s: Surcharge, base: number, kg: number): number {
  switch (s.kind) {
    case "flat":
      return s.cents ?? 0;
    case "percent":
      return Math.round((base * (s.percent ?? 0)) / 100);
    case "perKg":
      return Math.round(Math.max(0, kg - (s.aboveKg ?? 0)) * (s.centsPerKg ?? 0));
  }
  return 0;
}

function baseRate(table: RateTable, kg: number): number | null {
  const row = table.find(([maxKg]) => kg <= maxKg);
  return row !== undefined ? row[1] : null;
}

interface Quote {
  service: Service;
  cents: number;
  price: string;
  surcharges: string[];
}

interface QuoteResult {
  zone: Zone;
  remote: boolean;
  quotes: Quote[];
}

function quote(country: string, postcode: string, parcels: Parcel[]): QuoteResult {
  const code = country.trim().toUpperCase();
  const zone = ZONE_BY_COUNTRY.get(code);
  if (zone === undefined) throw new Error(`no shipping to ${code}`);
  const remote = isRemote(code, removeSpaces(postcode));
  const infos = parcels.map((p, i) => {
    if (!(p.weightKg > 0) || [p.lengthCm, p.widthCm, p.heightCm].some((d) => !(d > 0))) {
      throw new Error(`parcel ${i + 1}: invalid size or weight`);
    }
    return new ParcelInfo(p);
  });

  const quotes: Quote[] = [];
  for (const { service, table } of RATES.get(zone) ?? []) {
    let cents = 0;
    const labels = new Set<string>();
    let available = infos.length > 0;
    for (const info of infos) {
      const base = baseRate(table, info.billableKg);
      if (base === null) {
        available = false;
        break;
      }
      cents += base;
      for (const s of surchargesFor(info, service, remote)) {
        cents += surchargeCents(s, base, info.billableKg);
        labels.add(s.label);
      }
    }
    if (available) quotes.push({ service, cents, price: `EUR ${(cents / 100).toFixed(2)}`, surcharges: [...labels] });
  }
  quotes.sort((a, b) => a.cents - b.cents);
  return { zone, remote, quotes };
}

interface Request {
  country: string;
  postcode?: string;
  parcels: Parcel[];
}

interface ErrorResult {
  error: string;
}

function errorJson(message: string): string {
  const e: ErrorResult = { error: message };
  return JSON.stringify(e);
}

// Called by the app: {"country": "DE", "postcode": "10115", "parcels": [...]}
export function quoteShipping(requestJson: string): string {
  const req = tryParse(requestJson) as Request | null;
  if (req === null) return errorJson("invalid json");
  try {
    return JSON.stringify(quote(req.country, req.postcode ?? "", req.parcels));
  } catch (e) {
    return errorJson(e instanceof Error ? e.message : String(e));
  }
}
