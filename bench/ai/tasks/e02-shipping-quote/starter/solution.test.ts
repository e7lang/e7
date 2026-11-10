import { assertEqual } from "e7:test";
import { quoteShipping } from "./solution.ts";

const config = JSON.stringify({
  zones: [
    { name: "domestic", countries: ["DE"], baseCents: 490, perKgCents: 100, freeOverCents: 5000 },
    { name: "eu", countries: ["FR", "NL", "AT"], baseCents: 990, perKgCents: 250 },
  ],
});

export function testDomestic(): void {
  const order = '{"country":"de","items":[{"sku":"mug","priceCents":1200,"qty":2,"weightGrams":800}]}';
  assertEqual(quoteShipping(config, order), '{"zone":"domestic","weightKg":2,"cents":590}');
}

export function testFreeShipping(): void {
  const order = '{"country":"DE","items":[{"sku":"lamp","priceCents":5000,"qty":1,"weightGrams":3000}]}';
  assertEqual(quoteShipping(config, order), '{"zone":"domestic","weightKg":3,"cents":0}');
}

export function testNoZone(): void {
  const order = '{"country":"US","items":[{"sku":"mug","priceCents":1200,"qty":1,"weightGrams":400}]}';
  assertEqual(quoteShipping(config, order), '{"error":"no zone for US"}');
}

export function testInvalidOrder(): void {
  assertEqual(quoteShipping(config, '{"country":'), '{"error":"invalid order"}');
}
