import { assertEqual } from "e7:test";
import { checkoutJson } from "./solution.ts";

export function testSimpleCart(): void {
  const cart = '{"lines":[{"sku":"book","unitCents":1250,"qty":2}],"rules":[]}';
  assertEqual(checkoutJson(cart, "NY"), '{"subtotal":2500,"discount":0,"tax":100,"total":2600,"taxFreeSkus":[]}');
}

export function testBadQuantity(): void {
  const cart = '{"lines":[{"sku":"pen","unitCents":100,"qty":0}],"rules":[]}';
  assertEqual(checkoutJson(cart, "CA"), '{"error":"bad quantity for pen"}');
}
