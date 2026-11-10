`original.ts` renders the plain-text invoice our web shop prints and attaches to receipts, with VAT per tax
category. The point-of-sale app has to print exactly the same invoices offline, so please port it to E7 as
`solution.ts`. The app only calls `renderInvoice`, so keep that function with the same signature and the same
text, character for character, for every input; everything else can change as E7 needs.

```ts
export function renderInvoice(invoiceJson: string): string
```

`solution.test.ts` has two examples.
