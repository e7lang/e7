import { assertEqual } from "e7:test";
import { quoteShipping } from "./solution.ts";

export function testDomesticSmallParcel(): void {
  const request = '{"country":"DE","postcode":"10115","parcels":[{"weightKg":0.8,"lengthCm":30,"widthCm":20,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"domestic","remote":false,"quotes":[{"service":"standard","cents":549,"price":"EUR 5.49","surcharges":[]},{"service":"economy","cents":599,"price":"EUR 5.99","surcharges":[]},{"service":"express","cents":1617,"price":"EUR 16.17","surcharges":["fuel"]}]}');
}

export function testNoShipping(): void {
  const request = '{"country":"BR","postcode":"01310-100","parcels":[{"weightKg":1,"lengthCm":10,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"error":"no shipping to BR"}');
}
