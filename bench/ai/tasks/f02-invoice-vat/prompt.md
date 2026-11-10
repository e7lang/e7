**Bug: VAT on some invoices is off by a cent**

Our accountant flagged invoices whose VAT does not match the law. `solution.ts` (E7) computes the invoice
totals; all amounts in its result are integer cents.

Example: three stickers at 0.99 € each, on three lines, all at 20 % VAT:

```ts
invoiceTotals('{"lines":[{"sku":"sticker-a","unitPrice":0.99,"qty":1,"vatRate":20},{"sku":"sticker-b","unitPrice":0.99,"qty":1,"vatRate":20},{"sku":"sticker-c","unitPrice":0.99,"qty":1,"vatRate":20}]}')
```

Actual:

```
{"lines":[{"sku":"sticker-a","net":99},{"sku":"sticker-b","net":99},{"sku":"sticker-c","net":99}],"vatGroups":[{"rate":20,"net":297,"vat":60}],"net":297,"vat":60,"gross":357}
```

Expected (20 % of 2.97 € is 0.594 €, so the VAT is 0.59 €):

```
{"lines":[{"sku":"sticker-a","net":99},{"sku":"sticker-b","net":99},{"sku":"sticker-c","net":99}],"vatGroups":[{"rate":20,"net":297,"vat":59}],"net":297,"vat":59,"gross":356}
```

The accountant's rule: the VAT shown for a rate must be that rate applied to the net amount shown for that rate,
rounded to the nearest cent (half a cent rounds up). The total VAT is the sum of the rates' VAT, and gross is net
plus VAT. Invoices with one line per rate look right.

Please fix `solution.ts`. The exported API and the JSON result format stay as they are:

```ts
export function invoiceTotals(invoiceJson: string): string
```

Everything else (line net amounts and discounts, the order of `vatGroups` by rate, the error results) must keep
working as it does now. `solution.test.ts` has the existing tests.
