import { assertEqual } from "e7:test";
import { renderInvoice } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testMixedCategoriesFrance(): void {
  const invoice = '{"number":"INV-2024-0100","country":"fr","currency":"EUR","lines":[{"description":"Coffee beans 1kg","qty":3,"unitPrice":14.99,"category":"reduced"},{"description":"Espresso machine with milk frother","unitPrice":349,"discountPct":15},{"description":"Barista course","unitPrice":120,"category":"exempt"},{"description":"Tamper","qty":1,"unitPrice":19.95,"category":"standard"},{"description":"Newspaper","unitPrice":2.4,"category":"zero"}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2024-0100 (fr, EUR)\n   3 x Coffee beans 1kg         @    14.99 =     44.97  5.5%\n   1 x Espresso machine with... @   349.00 -15% =    296.65  20%\n   1 x Barista course           @   120.00 =    120.00  exempt*\n   1 x Tamper                   @    19.95 =     19.95  20%\n   1 x Newspaper                @     2.40 =      2.40  0%\nNet                                   483.97\nVAT 5.5% on 44.97                       2.47\nVAT 20% on 316.60                      63.32\nVAT 0% on 2.40                          0.00\nTotal EUR                             549.76\n* exempt from VAT');
}

export function testCategoryOrderFollowsFirstUse(): void {
  const invoice = '{"number":"INV-2025-0001","country":"NL","currency":"EUR","lines":[{"description":"Bread","qty":2,"unitPrice":3.35,"category":"reduced"},{"description":"Wine","qty":2,"unitPrice":8.99},{"description":"Cheese","qty":0.75,"unitPrice":21.4,"category":"reduced"}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2025-0001 (NL, EUR)\n   2 x Bread                    @     3.35 =      6.70  9%\n   2 x Wine                     @     8.99 =     17.98  21%\n0.75 x Cheese                   @    21.40 =     16.05  9%\nNet                                    40.73\nVAT 9% on 22.75                         2.05\nVAT 21% on 17.98                        3.78\nTotal EUR                              46.56');
}

export function testRoundingPerCategory(): void {
  const invoice = '{"number":"INV-2025-0002","country":"DE","currency":"EUR","lines":[{"description":"Screw","qty":7,"unitPrice":0.15},{"description":"Nut","qty":3,"unitPrice":0.33},{"description":"Washer","qty":11,"unitPrice":0.07,"discountPct":10}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2025-0002 (DE, EUR)\n   7 x Screw                    @     0.15 =      1.05  19%\n   3 x Nut                      @     0.33 =      0.99  19%\n  11 x Washer                   @     0.07 -10% =      0.69  19%\nNet                                     2.73\nVAT 19% on 2.73                         0.52\nTotal EUR                               3.25');
}

export function testOnlyExempt(): void {
  const invoice = '{"number":"INV-2025-0003","country":"DE","currency":"CHF","lines":[{"description":"Medical consultation","qty":1,"unitPrice":85,"category":"exempt"}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2025-0003 (DE, CHF)\n   1 x Medical consultation     @    85.00 =     85.00  exempt*\nNet                                    85.00\nTotal CHF                              85.00\n* exempt from VAT');
}

export function testFullDiscountAndLongNumber(): void {
  const invoice = '{"number":"INV-2025-123456","country":"NL","currency":"EUR","lines":[{"description":"Gift","unitPrice":10,"discountPct":100},{"description":"Card","unitPrice":1.5}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2025-123456 (NL, EUR)\n   1 x Gift                     @    10.00 -100% =      0.00  21%\n   1 x Card                     @     1.50 =      1.50  21%\nNet                                     1.50\nVAT 21% on 1.50                         0.32\nTotal EUR                               1.82');
}

export function testNoLines(): void {
  assertEqual(renderInvoice('{"number":"INV-2025-0004","country":"FR","currency":"EUR","lines":[]}'), 'Invoice INV-2025-0004 (FR, EUR)\nNet                                     0.00\nTotal EUR                               0.00');
}

export function testBadInvoiceNumber(): void {
  const invoice = '{"number":"INV-25-0004","country":"FR","currency":"EUR","lines":[{"description":"x","unitPrice":1}]}';
  assertEqual(renderInvoice(invoice), 'error: invalid invoice number "INV-25-0004"');
}

export function testZeroQuantity(): void {
  const invoice = '{"number":"INV-2025-0005","country":"DE","currency":"EUR","lines":[{"description":"a","unitPrice":1},{"description":"b","qty":0,"unitPrice":1}]}';
  assertEqual(renderInvoice(invoice), 'error: line 2: quantity must be positive');
}

export function testNegativeQuantity(): void {
  const invoice = '{"number":"INV-2025-0005","country":"DE","currency":"EUR","lines":[{"description":"a","qty":-1,"unitPrice":1}]}';
  assertEqual(renderInvoice(invoice), 'error: line 1: quantity must be positive');
}

export function testDiscountOutOfRange(): void {
  const invoice = '{"number":"INV-2025-0006","country":"DE","currency":"EUR","lines":[{"description":"a","unitPrice":1,"discountPct":120}]}';
  assertEqual(renderInvoice(invoice), 'error: line 1: discount must be 0-100');
}

export function testMissingDescription(): void {
  const invoice = '{"number":"INV-2025-0007","country":"DE","currency":"EUR","lines":[{"description":"","unitPrice":1}]}';
  assertEqual(renderInvoice(invoice), 'error: line 1: missing description');
}

export function testUnknownCategory(): void {
  const invoice = '{"number":"INV-2025-0008","country":"NL","currency":"EUR","lines":[{"description":"Yacht","unitPrice":1000000,"category":"luxury"}]}';
  assertEqual(renderInvoice(invoice), 'error: line 1: unknown tax category "luxury"');
}

export function testInvalidJson(): void {
  assertEqual(renderInvoice('{"number":"INV-2025-0009",'), 'error: invalid json');
}
