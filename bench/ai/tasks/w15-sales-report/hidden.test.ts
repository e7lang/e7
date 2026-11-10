import { assertEqual } from "e7:test";
import { salesReport } from "./solution.ts";

interface Row { region: string; orders: number; units: number; revenueCents: number; customers: number; topSku: string }
interface Report { regions: Row[]; totalRevenueCents: number; skipped: number }

function report(json: string): Report {
  return JSON.parse(salesReport(json)) as Report;
}

const sample = `[
  {"id": "1", "region": "EU", "customer": "anna", "status": "paid",
   "items": [{"sku": "mug", "qty": 2, "unitPriceCents": 1200}, {"sku": "tea", "qty": 1, "unitPriceCents": 650}]},
  {"id": "2", "region": "US", "customer": "bob", "status": "paid", "discountPercent": 25,
   "items": [{"sku": "kettle", "qty": 1, "unitPriceCents": 4999}]},
  {"id": "3", "region": "EU", "customer": "anna", "status": "paid",
   "items": [{"sku": "tea", "qty": 3, "unitPriceCents": 650}]},
  {"id": "4", "region": "EU", "customer": "carl", "status": "refunded",
   "items": [{"sku": "mug", "qty": 50, "unitPriceCents": 1200}]},
  {"id": "5", "region": "APAC", "customer": "dee", "status": "pending",
   "items": [{"sku": "mug", "qty": 1, "unitPriceCents": 1200}]},
  {"id": "6", "region": "US", "customer": "eve", "status": "paid", "note": "gift",
   "items": [{"sku": "mug", "qty": 1, "unitPriceCents": 1200}, {"sku": "tea", "qty": 1, "unitPriceCents": 650}]}
]`;

export function testSampleExactOutput(): void {
  // EU: 2400 + 650 + 1950 = 5000; US: round(4999 * 75 / 100) = 3749, + 1850 = 5599
  assertEqual(salesReport(sample),
    '{"regions":[' +
    '{"region":"US","orders":2,"units":3,"revenueCents":5599,"customers":2,"topSku":"kettle"},' +
    '{"region":"EU","orders":2,"units":6,"revenueCents":5000,"customers":1,"topSku":"tea"}' +
    '],"totalRevenueCents":10599,"skipped":2}');
}

export function testEmpty(): void {
  assertEqual(salesReport("[]"), '{"regions":[],"totalRevenueCents":0,"skipped":0}');
  assertEqual(salesReport(" [ ] "), '{"regions":[],"totalRevenueCents":0,"skipped":0}');
}

export function testOnlySkipped(): void {
  const r = report('[{"id":"a","region":"EU","customer":"x","status":"cancelled","items":[]},' +
    '{"id":"b","region":"EU","customer":"x","status":"PAID","items":[]}]');
  assertEqual(r, { regions: [], totalRevenueCents: 0, skipped: 2 });
}

export function testInvalidJson(): void {
  for (const bad of ["", "nope", "[", '[{"id":"1",}]', "{'a':1}", "[1,2"]) {
    assertEqual(salesReport(bad), '{"error":"invalid json"}', bad);
  }
}

export function testDiscountRoundingPerOrder(): void {
  // 3 orders of 5 cents at 50% off: 2.5 -> 3 each (Math.round), total 9, not round(7.5) = 8
  const one = '{"id":"x","region":"R","customer":"c","status":"paid","discountPercent":50,"items":[{"sku":"s","qty":1,"unitPriceCents":5}]}';
  const r = report(`[${one},${one},${one}]`);
  assertEqual(r.regions[0].revenueCents, 9);
  // 333 at 33% off: 333 * 67 / 100 = 223.11 -> 223
  const r2 = report('[{"id":"x","region":"R","customer":"c","status":"paid","discountPercent":33,"items":[{"sku":"s","qty":1,"unitPriceCents":333}]}]');
  assertEqual(r2.totalRevenueCents, 223);
  // fractional percent
  const r3 = report('[{"id":"x","region":"R","customer":"c","status":"paid","discountPercent":12.5,"items":[{"sku":"s","qty":4,"unitPriceCents":1999}]}]');
  assertEqual(r3.totalRevenueCents, 6997); // 7996 * 0.875 = 6996.5 -> 6997
}

export function testTopSkuTiesAndAccumulation(): void {
  const r = report(`[
    {"id":"1","region":"R","customer":"a","status":"paid","items":[{"sku":"pear","qty":2,"unitPriceCents":1},{"sku":"fig","qty":1,"unitPriceCents":1}]},
    {"id":"2","region":"R","customer":"b","status":"paid","items":[{"sku":"fig","qty":1,"unitPriceCents":1},{"sku":"apple","qty":1,"unitPriceCents":1}]}
  ]`);
  assertEqual(r.regions[0].topSku, "fig"); // fig 2, pear 2, apple 1: tie -> "fig" < "pear"
  const r2 = report(`[{"id":"1","region":"R","customer":"a","status":"paid","items":[
    {"sku":"b","qty":1,"unitPriceCents":1},{"sku":"a","qty":1,"unitPriceCents":1},{"sku":"b","qty":1,"unitPriceCents":1}]}]`);
  assertEqual(r2.regions[0].topSku, "b");
  // uppercase sorts before lowercase with plain < order
  const r3 = report(`[{"id":"1","region":"R","customer":"a","status":"paid","items":[
    {"sku":"tea","qty":1,"unitPriceCents":1},{"sku":"Tea","qty":1,"unitPriceCents":1}]}]`);
  assertEqual(r3.regions[0].topSku, "Tea");
}

export function testOrderWithoutItems(): void {
  const r = report(`[{"id":"1","region":"X","customer":"a","status":"paid","items":[]},
                     {"id":"2","region":"X","customer":"b","status":"paid","items":[]}]`);
  assertEqual(r.regions, [{ region: "X", orders: 2, units: 0, revenueCents: 0, customers: 2, topSku: "" }]);
}

export function testSortingByRevenueThenName(): void {
  const o = (id: string, region: string, cents: number) =>
    `{"id":"${id}","region":"${region}","customer":"c${id}","status":"paid","items":[{"sku":"s","qty":1,"unitPriceCents":${cents}}]}`;
  const r = report(`[${o("1", "beta", 100)},${o("2", "Alpha", 100)},${o("3", "alpha", 100)},${o("4", "gamma", 500)},${o("5", "delta", 50)}]`);
  assertEqual(r.regions.map((x) => x.region), ["gamma", "Alpha", "alpha", "beta", "delta"]);
  assertEqual(r.totalRevenueCents, 850);
}

export function testRegionNamesAreExact(): void {
  const o = (region: string) =>
    `{"id":"1","region":"${region}","customer":"c","status":"paid","items":[{"sku":"s","qty":1,"unitPriceCents":10}]}`;
  const r = report(`[${o("EU")},${o("eu")},${o("EU ")},${o("EU")}]`);
  assertEqual(r.regions.map((x) => `${x.region}|${x.orders}`), ["EU|2", "EU |1", "eu|1"]);
}

export function testDistinctCustomersAndUnicode(): void {
  const r = report(`[
    {"id":"1","region":"Österreich","customer":"zoë","status":"paid","items":[{"sku":"Käse","qty":2,"unitPriceCents":700}]},
    {"id":"2","region":"Österreich","customer":"zoë","status":"paid","items":[{"sku":"Brot","qty":1,"unitPriceCents":300}]},
    {"id":"3","region":"Österreich","customer":"Zoë","status":"paid","items":[]}
  ]`);
  assertEqual(r.regions, [{ region: "Österreich", orders: 3, units: 3, revenueCents: 1700, customers: 2, topSku: "Käse" }]);
}
