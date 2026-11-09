import { assertEqual } from "e7:test";
import { salesReport } from "./solution.ts";

export function testOneOrder(): void {
  const input = JSON.stringify([
    { id: "o1", region: "EU", customer: "c7", status: "paid", discountPercent: 10,
      items: [{ sku: "pen", qty: 3, unitPriceCents: 250 }, { sku: "pad", qty: 1, unitPriceCents: 900 }] },
  ]);
  assertEqual(salesReport(input),
    '{"regions":[{"region":"EU","orders":1,"units":4,"revenueCents":1485,"customers":1,"topSku":"pen"}],"totalRevenueCents":1485,"skipped":0}');
}

export function testInvalid(): void {
  assertEqual(salesReport("[{"), '{"error":"invalid json"}');
}
