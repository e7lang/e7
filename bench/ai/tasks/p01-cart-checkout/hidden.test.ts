import { assertEqual } from "e7:test";
import { checkoutJson } from "./solution.ts";

// Expected strings were produced by running original.ts on Node.js.

export function testPercentAndTax(): void {
  const c = '{"lines":[{"sku":"a","unitCents":1999,"qty":3}],"rules":[{"kind":"percent","percent":15}]}';
  assertEqual(checkoutJson(c, "CA"), '{"subtotal":5997,"discount":900,"tax":370,"total":5467,"taxFreeSkus":[]}');
}

export function testStateIsCaseInsensitive(): void {
  const c = '{"lines":[{"sku":"a","unitCents":1000,"qty":1}],"rules":[]}';
  assertEqual(checkoutJson(c, "tx"), '{"subtotal":1000,"discount":0,"tax":63,"total":1063,"taxFreeSkus":[]}');
}

export function testUnknownStateNoTax(): void {
  const c = '{"lines":[{"sku":"a","unitCents":1000,"qty":1}],"rules":[]}';
  assertEqual(checkoutJson(c, "OR"), '{"subtotal":1000,"discount":0,"tax":0,"total":1000,"taxFreeSkus":[]}');
}

export function testFixedCappedAtSubtotal(): void {
  const c = '{"lines":[{"sku":"a","unitCents":300,"qty":1}],"rules":[{"kind":"fixed","cents":500}]}';
  assertEqual(checkoutJson(c, "NY"), '{"subtotal":300,"discount":300,"tax":0,"total":0,"taxFreeSkus":[]}');
}

export function testBogo(): void {
  const c = '{"lines":[{"sku":"sock","unitCents":450,"qty":5},{"sku":"hat","unitCents":2000,"qty":1}],"rules":[{"kind":"bogo","sku":"sock"},{"kind":"bogo","sku":"none"}]}';
  assertEqual(checkoutJson(c, "NY"), '{"subtotal":4250,"discount":900,"tax":134,"total":3484,"taxFreeSkus":[]}');
}

export function testStackedRules(): void {
  const c = '{"lines":[{"sku":"a","unitCents":2500,"qty":2}],"rules":[{"kind":"percent","percent":10},{"kind":"fixed","cents":250}]}';
  assertEqual(checkoutJson(c, "CA"), '{"subtotal":5000,"discount":750,"tax":308,"total":4558,"taxFreeSkus":[]}');
}

export function testTaxFreeFoodProportionalDiscount(): void {
  const c = '{"lines":[{"sku":"bread","unitCents":400,"qty":2,"tags":["food"]},{"sku":"mug","unitCents":1200,"qty":1,"tags":["kitchen"]},{"sku":"apple","unitCents":100,"qty":3,"tags":["food","fresh"]},{"sku":"bread","unitCents":400,"qty":1,"tags":["food"]}],"rules":[{"kind":"percent","percent":20}]}';
  assertEqual(checkoutJson(c, "CA"), '{"subtotal":2700,"discount":540,"tax":70,"total":2230,"taxFreeSkus":["apple","bread"]}');
}

export function testEmptyCart(): void {
  assertEqual(checkoutJson('{"lines":[],"rules":[{"kind":"percent","percent":50}]}', "CA"), '{"subtotal":0,"discount":0,"tax":0,"total":0,"taxFreeSkus":[]}');
}

export function testFractionalQuantity(): void {
  const c = '{"lines":[{"sku":"x","unitCents":100,"qty":1.5}],"rules":[]}';
  assertEqual(checkoutJson(c, "CA"), '{"error":"bad quantity for x"}');
}

export function testNegativeQuantity(): void {
  const c = '{"lines":[{"sku":"ok","unitCents":100,"qty":1},{"sku":"neg","unitCents":100,"qty":-2}],"rules":[]}';
  assertEqual(checkoutJson(c, "CA"), '{"error":"bad quantity for neg"}');
}

export function testInvalidJson(): void {
  assertEqual(checkoutJson('{"lines":[', "CA"), '{"error":"invalid json"}');
}
