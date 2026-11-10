import { assertEqual } from "e7:test";
import { renderInvoice } from "./solution.ts";

export function testSimpleInvoice(): void {
  const invoice = '{"number":"INV-2024-0042","country":"DE","currency":"EUR","lines":[{"description":"Desk lamp","qty":2,"unitPrice":24.9},{"description":"Cookbook","unitPrice":18,"category":"reduced"}]}';
  assertEqual(renderInvoice(invoice), 'Invoice INV-2024-0042 (DE, EUR)\n   2 x Desk lamp                @    24.90 =     49.80  19%\n   1 x Cookbook                 @    18.00 =     18.00  7%\nNet                                    67.80\nVAT 19% on 49.80                        9.46\nVAT 7% on 18.00                         1.26\nTotal EUR                              78.52');
}

export function testUnknownCountry(): void {
  const invoice = '{"number":"INV-2024-0043","country":"US","currency":"USD","lines":[{"description":"Desk lamp","unitPrice":24.9}]}';
  assertEqual(renderInvoice(invoice), 'error: no VAT rates for US');
}
