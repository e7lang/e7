import { assertEqual } from "e7:test";
import { quoteShipping } from "./solution.ts";

const config = JSON.stringify({
  zones: [
    { name: "domestic", countries: ["DE"], baseCents: 490, perKgCents: 100, freeOverCents: 5000, expressMultiplier: 1.5, oversizeCents: 1500 },
    { name: "eu", countries: ["FR", "NL", "AT"], baseCents: 990, perKgCents: 250, expressMultiplier: 1.75 },
    { name: "uk", countries: ["GB"], baseCents: 1290, perKgCents: 300, oversizeCents: 2000 },
    { name: "world", countries: ["*"], baseCents: 1990, perKgCents: 500 },
  ],
});

// the configuration the app shipped with before this change (no new fields anywhere)
const oldConfig = JSON.stringify({
  zones: [
    { name: "domestic", countries: ["DE"], baseCents: 490, perKgCents: 100, freeOverCents: 5000 },
    { name: "eu", countries: ["FR", "NL", "AT"], baseCents: 990, perKgCents: 250 },
  ],
});

function order(country: string, items: string, service: string): string {
  return `{"country":"${country}","items":[${items}]${service === "" ? "" : `,"service":"${service}"`}}`;
}

const mug = '{"sku":"mug","priceCents":1200,"qty":2,"weightGrams":800}';
const lamp = '{"sku":"lamp","priceCents":5000,"qty":1,"weightGrams":3000}';
const sofa = '{"sku":"sofa","priceCents":40000,"qty":2,"weightGrams":20000,"oversize":true}';
const rug = '{"sku":"rug","priceCents":9000,"qty":1,"weightGrams":4000,"oversize":false}';

export function testOldConfigAndOrdersUnchanged(): void {
  assertEqual(quoteShipping(oldConfig, order("de", mug, "")), '{"zone":"domestic","weightKg":2,"cents":590}');
  assertEqual(quoteShipping(oldConfig, order("DE", lamp, "")), '{"zone":"domestic","weightKg":3,"cents":0}');
  assertEqual(quoteShipping(oldConfig, order(" nl ", lamp, "")), '{"zone":"eu","weightKg":3,"cents":1490}');
  assertEqual(quoteShipping(oldConfig, order("US", mug, "")), '{"error":"no zone for US"}');
}

export function testStandardExplicit(): void {
  assertEqual(quoteShipping(config, order("DE", mug, "standard")), '{"zone":"domestic","weightKg":2,"cents":590}');
  assertEqual(quoteShipping(config, order("FR", mug, "standard")), '{"zone":"eu","weightKg":2,"cents":1240}');
}

export function testWildcardZone(): void {
  assertEqual(quoteShipping(config, order("JP", mug, "")), '{"zone":"world","weightKg":2,"cents":2490}');
}

export function testExpressMultiplierRounded(): void {
  // 990 + 250 = 1240, * 1.75 = 2170
  assertEqual(quoteShipping(config, order("FR", mug, "express")), '{"zone":"eu","weightKg":2,"cents":2170}');
  // 990, * 1.75 = 1732.5 -> 1733
  const light = '{"sku":"card","priceCents":300,"qty":1,"weightGrams":20}';
  assertEqual(quoteShipping(config, order("AT", light, "express")), '{"zone":"eu","weightKg":1,"cents":1733}');
}

export function testExpressIgnoresFreeShipping(): void {
  // 490 + 2 * 100 = 690, * 1.5 = 1035 even though the subtotal reaches 5000
  assertEqual(quoteShipping(config, order("DE", lamp, "express")), '{"zone":"domestic","weightKg":3,"cents":1035}');
}

export function testExpressNotAvailable(): void {
  assertEqual(quoteShipping(config, order("gb", mug, "express")), '{"error":"express not available to GB"}');
  assertEqual(quoteShipping(config, order("JP", mug, "express")), '{"error":"express not available to JP"}');
}

export function testUnknownService(): void {
  assertEqual(quoteShipping(config, order("DE", mug, "overnight")), '{"error":"unknown service: overnight"}');
  assertEqual(quoteShipping(config, order("DE", mug, "Express")), '{"error":"unknown service: Express"}');
}

export function testOversizeSurchargePerUnit(): void {
  // 40 kg: 1290 + 39 * 300 = 12990, plus 2 oversize units * 2000
  assertEqual(quoteShipping(config, order("GB", sofa, "")), '{"zone":"uk","weightKg":40,"cents":16990}');
  // oversize false costs nothing extra: 4 kg -> 1290 + 900
  assertEqual(quoteShipping(config, order("GB", rug, "")), '{"zone":"uk","weightKg":4,"cents":2190}');
}

export function testOversizeStillChargedWithFreeShipping(): void {
  assertEqual(quoteShipping(config, order("DE", sofa, "")), '{"zone":"domestic","weightKg":40,"cents":3000}');
}

export function testOversizeAddedAfterExpress(): void {
  // 490 + 40 * 100 = 4490 (41 kg), * 1.5 = 6735, + 2 * 1500
  assertEqual(quoteShipping(config, order("DE", `${sofa},${mug.replace('"qty":2', '"qty":1')}`, "express")), '{"zone":"domestic","weightKg":41,"cents":9735}');
}

export function testZoneWithoutOversizeCents(): void {
  // the eu zone has no oversizeCents: no surcharge
  assertEqual(quoteShipping(config, order("NL", sofa, "")), '{"zone":"eu","weightKg":40,"cents":10740}');
}

export function testErrorOrder(): void {
  assertEqual(quoteShipping(config, order("DE", "", "express")), '{"error":"empty order"}');
  assertEqual(quoteShipping(config, order("XX", mug, "fast")), '{"error":"unknown service: fast"}');
  const bad = '{"sku":"pen","priceCents":100,"qty":0,"weightGrams":10}';
  assertEqual(quoteShipping(config, order("GB", bad, "express")), '{"error":"express not available to GB"}');
  assertEqual(quoteShipping(config, order("GB", bad, "")), '{"error":"bad quantity for pen"}');
}

export function testInvalidJson(): void {
  assertEqual(quoteShipping("{", order("DE", mug, "")), '{"error":"invalid config"}');
  assertEqual(quoteShipping(config, "[1,"), '{"error":"invalid order"}');
}
