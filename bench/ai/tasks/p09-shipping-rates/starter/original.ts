// Shipping quotes for the checkout: zone by destination country, rate by billable weight, then surcharges.
// All amounts are in euro cents.

export enum Service {
  Economy = "economy",
  Standard = "standard",
  Express = "express",
}

type Zone = "domestic" | "eu" | "europe" | "world";
type RateTable = [maxKg: number, cents: number][]; // ascending by weight

const ZONE_BY_COUNTRY: Record<string, Zone> = {
  DE: "domestic",
  AT: "eu", FR: "eu", NL: "eu", IT: "eu", ES: "eu", DK: "eu",
  CH: "europe", GB: "europe", NO: "europe",
  US: "world", CA: "world", JP: "world", AU: "world",
};

const RATES: Record<Zone, Partial<Record<Service, RateTable>>> = {
  domestic: {
    economy: [[1, 399], [5, 599], [10, 899], [31.5, 1499]],
    standard: [[2, 549], [10, 799], [31.5, 1699]],
    express: [[5, 1490], [20, 2490]],
  },
  eu: {
    standard: [[2, 1299], [5, 1799], [10, 2499], [31.5, 3999]],
    express: [[5, 3490], [20, 5990]],
  },
  europe: {
    standard: [[2, 1599], [5, 2299], [20, 4499]],
    express: [[5, 4490], [20, 7990]],
  },
  world: {
    standard: [[2, 2999], [5, 4499], [10, 6999], [20, 9999]],
  },
};

// remote areas cost extra: postcodes per country
const REMOTE_POSTCODES: { [country: string]: RegExp } = {
  DE: /^(18565|25849|25859|25863|25869|27498)/,
  GB: /^(BT|HS|IV4[0-9]|KW|ZE)/i,
  DK: /^(37|38|39)\d\d$/,
};

type Surcharge =
  | { kind: "flat"; label: string; cents: number }
  | { kind: "percent"; label: string; percent: number }
  | { kind: "perKg"; label: string; centsPerKg: number; aboveKg: number };

export interface Parcel {
  weightKg: number;
  lengthCm: number;
  widthCm: number;
  heightCm: number;
}

interface Billable {
  readonly billableKg: number;
  readonly oversize: boolean;
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
  if (service === Service.Express) list.push({ kind: "percent", label: "fuel", percent: 8.5 });
  if (parcel.billableKg > 20) list.push({ kind: "perKg", label: "heavy", centsPerKg: 45, aboveKg: 20 });
  return list;
}

function surchargeCents(s: Surcharge, base: number, kg: number): number {
  switch (s.kind) {
    case "flat":
      return s.cents;
    case "percent":
      return Math.round((base * s.percent) / 100);
    case "perKg":
      return Math.round(Math.max(0, kg - s.aboveKg) * s.centsPerKg);
  }
}

const baseRate = (table: RateTable, kg: number) => table.find(([maxKg]) => kg <= maxKg)?.[1];

export function quote(country: string, postcode: string, parcels: Parcel[]) {
  const code = country.trim().toUpperCase();
  if (!(code in ZONE_BY_COUNTRY)) throw new Error(`no shipping to ${code}`);
  const zone = ZONE_BY_COUNTRY[code];
  const remote = REMOTE_POSTCODES[code]?.test(postcode.replace(/\s+/g, "")) ?? false;
  const infos = parcels.map((p, i) => {
    if (!(p.weightKg > 0) || [p.lengthCm, p.widthCm, p.heightCm].some((d) => !(d > 0))) {
      throw new Error(`parcel ${i + 1}: invalid size or weight`);
    }
    return new ParcelInfo(p);
  });

  const quotes = [];
  for (const [service, table] of Object.entries(RATES[zone]) as [Service, RateTable][]) {
    let cents = 0;
    const labels = new Set<string>();
    let available = infos.length > 0;
    for (const info of infos) {
      const base = baseRate(table, info.billableKg);
      if (base === undefined) {
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

// Called by the app: {"country": "DE", "postcode": "10115", "parcels": [...]}
export function quoteShipping(requestJson: string): string {
  try {
    const req = JSON.parse(requestJson) as { country: string; postcode: string; parcels: Parcel[] };
    return JSON.stringify(quote(req.country, req.postcode ?? "", req.parcels));
  } catch (e) {
    return JSON.stringify({ error: e instanceof SyntaxError ? "invalid json" : (e as Error).message });
  }
}
