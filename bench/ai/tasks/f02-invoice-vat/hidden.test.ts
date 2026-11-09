import { assertEqual } from "e7:test";
import { invoiceTotals } from "./solution.ts";

export function testReportedStickers(): void {
  const inv = '{"lines":[{"sku":"sticker-a","unitPrice":0.99,"qty":1,"vatRate":20},{"sku":"sticker-b","unitPrice":0.99,"qty":1,"vatRate":20},{"sku":"sticker-c","unitPrice":0.99,"qty":1,"vatRate":20}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"sticker-a","net":99},{"sku":"sticker-b","net":99},{"sku":"sticker-c","net":99}],"vatGroups":[{"rate":20,"net":297,"vat":59}],"net":297,"vat":59,"gross":356}',
  );
}

export function testHalfCentRoundsUpOnTheRateTotal(): void {
  const inv = '{"lines":[{"sku":"a","unitPrice":5.02,"qty":1,"vatRate":10},{"sku":"b","unitPrice":5.03,"qty":1,"vatRate":10}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"a","net":502},{"sku":"b","net":503}],"vatGroups":[{"rate":10,"net":1005,"vat":101}],"net":1005,"vat":101,"gross":1106}',
  );
}

export function testSeveralLinesPerRate(): void {
  const inv = '{"lines":[{"sku":"espresso","unitPrice":1.47,"qty":1,"vatRate":20},{"sku":"muffin","unitPrice":1.9,"qty":1,"vatRate":5.5},{"sku":"juice","unitPrice":1.47,"qty":1,"vatRate":20},{"sku":"salad","unitPrice":3.9,"qty":1,"vatRate":5.5}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"espresso","net":147},{"sku":"muffin","net":190},{"sku":"juice","net":147},{"sku":"salad","net":390}],"vatGroups":[{"rate":5.5,"net":580,"vat":32},{"rate":20,"net":294,"vat":59}],"net":874,"vat":91,"gross":965}',
  );
}

export function testOneLinePerRate(): void {
  const inv = '{"lines":[{"sku":"book","unitPrice":8.4,"qty":1,"vatRate":5.5},{"sku":"pen","unitPrice":12.5,"qty":2,"vatRate":20}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"book","net":840},{"sku":"pen","net":2500}],"vatGroups":[{"rate":5.5,"net":840,"vat":46},{"rate":20,"net":2500,"vat":500}],"net":3340,"vat":546,"gross":3886}',
  );
}

export function testDiscountedLines(): void {
  const inv = '{"lines":[{"sku":"lamp","unitPrice":19.99,"qty":3,"vatRate":20,"discountPercent":15},{"sku":"bulb","unitPrice":3.49,"qty":5,"vatRate":20,"discountPercent":15}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"lamp","net":5097},{"sku":"bulb","net":1483}],"vatGroups":[{"rate":20,"net":6580,"vat":1316}],"net":6580,"vat":1316,"gross":7896}',
  );
}

export function testZeroAndSuperReducedRates(): void {
  const inv = '{"lines":[{"sku":"news","unitPrice":1.45,"qty":3,"vatRate":2.1},{"sku":"mag","unitPrice":4.2,"qty":1,"vatRate":2.1},{"sku":"stamp","unitPrice":1.29,"qty":4,"vatRate":0}]}';
  assertEqual(
    invoiceTotals(inv),
    '{"lines":[{"sku":"news","net":435},{"sku":"mag","net":420},{"sku":"stamp","net":516}],"vatGroups":[{"rate":0,"net":516,"vat":0},{"rate":2.1,"net":855,"vat":18}],"net":1371,"vat":18,"gross":1389}',
  );
}

export function testEmptyInvoice(): void {
  assertEqual(invoiceTotals('{"lines":[]}'), '{"lines":[],"vatGroups":[],"net":0,"vat":0,"gross":0}');
}

export function testErrors(): void {
  assertEqual(invoiceTotals('{"lines":[{"sku":"x"'), '{"error":"invalid invoice"}');
  assertEqual(invoiceTotals('{"lines":[{"sku":"x","unitPrice":"1","qty":1,"vatRate":20}]}'), '{"error":"invalid invoice"}');
  assertEqual(
    invoiceTotals('{"lines":[{"sku":"x","unitPrice":1,"qty":1,"vatRate":20},{"sku":"y","unitPrice":1,"qty":0,"vatRate":20}]}'),
    '{"error":"bad quantity on line 2"}',
  );
  assertEqual(invoiceTotals('{"lines":[{"sku":"x","unitPrice":1,"qty":1,"vatRate":19}]}'), '{"error":"bad VAT rate on line 1"}');
  assertEqual(
    invoiceTotals('{"lines":[{"sku":"x","unitPrice":1,"qty":1,"vatRate":20,"discountPercent":120}]}'),
    '{"error":"bad discount on line 1"}',
  );
}
