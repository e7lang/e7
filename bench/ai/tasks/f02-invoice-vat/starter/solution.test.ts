import { assertEqual } from "e7:test";
import { invoiceTotals } from "./solution.ts";

export function testTwoRates(): void {
  const inv = '{"lines":[{"sku":"pen","unitPrice":12.5,"qty":2,"vatRate":20},{"sku":"book","unitPrice":8.4,"qty":1,"vatRate":5.5}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"pen","net":2500},{"sku":"book","net":840}],"vatGroups":[{"rate":5.5,"net":840,"vat":46},{"rate":20,"net":2500,"vat":500}],"net":3340,"vat":546,"gross":3886}',
  );
}

export function testBadQuantity(): void {
  assertEqual(invoiceTotals('{"lines":[{"sku":"pen","unitPrice":1,"qty":1.5,"vatRate":20}]}'), '{"error":"bad quantity on line 1"}');
}
