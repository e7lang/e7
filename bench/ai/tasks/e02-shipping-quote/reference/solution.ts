// Shipping quotes for the checkout screen. The rate table comes from the backend as JSON so that
// prices can change without an app release; the order is the current cart.
import { tryParse } from "e7:json";

interface Zone {
  name: string;
  countries: string[]; // ISO codes, uppercase; "*" matches every country
  baseCents: number; // covers the first kilogram
  perKgCents: number; // each further started kilogram
  freeOverCents?: number; // free shipping when the merchandise subtotal reaches this
  expressMultiplier?: number; // express is offered only where this is set
  oversizeCents?: number; // surcharge per oversize unit
}

interface ShippingConfig {
  zones: Zone[];
}

interface OrderItem {
  sku: string;
  priceCents: number;
  qty: number;
  weightGrams: number;
  oversize?: boolean;
}

interface Order {
  country: string;
  items: OrderItem[];
  service?: string; // "standard" (default) or "express"
}

interface Quote {
  zone: string;
  weightKg: number;
  cents: number;
}

interface QuoteError {
  error: string;
}

type Service = "standard" | "express";

function errorJson(message: string): string {
  const e: QuoteError = { error: message };
  return JSON.stringify(e);
}

function findZone(config: ShippingConfig, country: string): Zone | null {
  for (const zone of config.zones) {
    const codes = new Set<string>(zone.countries);
    if (codes.has(country) || codes.has("*")) return zone;
  }
  return null;
}

function parseService(text: string | undefined): Service {
  if (text === undefined || text === "standard") return "standard";
  if (text === "express") return "express";
  throw new Error(`unknown service: ${text}`);
}

function quote(config: ShippingConfig, order: Order): Quote {
  if (order.items.length === 0) throw new Error("empty order");
  const service = parseService(order.service);
  const country = order.country.trim().toUpperCase();
  const zone = findZone(config, country);
  if (zone === null) throw new Error(`no zone for ${country}`);
  const multiplier = zone.expressMultiplier;
  if (service === "express" && multiplier === undefined) throw new Error(`express not available to ${country}`);

  let grams = 0;
  let subtotal = 0;
  let oversizeUnits = 0;
  for (const item of order.items) {
    if (!Number.isInteger(item.qty) || item.qty <= 0) throw new Error(`bad quantity for ${item.sku}`);
    grams += item.weightGrams * item.qty;
    subtotal += item.priceCents * item.qty;
    if (item.oversize === true) oversizeUnits += item.qty;
  }
  const weightKg = Math.max(1, Math.ceil(grams / 1000));
  let cents = zone.baseCents + zone.perKgCents * (weightKg - 1);
  switch (service) {
    case "standard":
      if (zone.freeOverCents !== undefined && subtotal >= zone.freeOverCents) cents = 0;
      break;
    case "express":
      cents = Math.round(cents * (multiplier ?? 1));
      break;
  }
  cents += oversizeUnits * (zone.oversizeCents ?? 0);
  return { zone: zone.name, weightKg, cents };
}

// Returns {"zone":...,"weightKg":...,"cents":...} or {"error":"..."}.
export function quoteShipping(configJson: string, orderJson: string): string {
  const config = tryParse(configJson) as ShippingConfig | null;
  if (config === null) return errorJson("invalid config");
  const order = tryParse(orderJson) as Order | null;
  if (order === null) return errorJson("invalid order");
  try {
    return JSON.stringify(quote(config, order));
  } catch (e) {
    return errorJson(e instanceof Error ? e.message : String(e));
  }
}
