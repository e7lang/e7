Checkout and the invoice PDF currently compute tax slightly differently, so we want one E7 script that both
use. Please write `solution.ts` with:

```ts
export type Rounding = "half-up" | "half-even";
export interface LineItem { sku: string; unitCents: number; quantity: number; taxRateBp: number }
export interface InvoiceTotals {
  subtotalCents: number; taxCents: number; totalCents: number;
  subtotal: string; tax: string; total: string;
}
export function formatMoney(cents: number, currency: string): string
export function lineTaxCents(item: LineItem, mode: Rounding): number
export function invoiceTotals(items: LineItem[], currency: string, mode: Rounding): InvoiceTotals
```

All amounts are integer cents. `unitCents` can be negative (credit lines). `taxRateBp` is the tax rate in basis
points (825 = 8.25%).

**Tax** is computed and rounded **per line**: the exact tax of a line is
`unitCents * quantity * taxRateBp / 10000` cents, rounded to a whole cent. Only exact halves depend on `mode`:
`"half-up"` rounds them away from zero (2.5 → 3, -2.5 → -3), `"half-even"` to the even neighbour (2.5 → 2,
3.5 → 4, -2.5 → -2). Everything else rounds to the nearest cent. Avoid floating-point surprises: these products
are integers, so the result must be exact.

**Totals:** `subtotalCents` is the sum of `unitCents * quantity`, `taxCents` the sum of the rounded line taxes,
`totalCents` their sum; `subtotal` / `tax` / `total` are the same three amounts formatted with `formatMoney`.
No items gives all zeros. A `quantity` that is not a positive integer throws `new Error("invalid quantity for <sku>")`.

**Formatting** (`formatMoney`): thousands are grouped in threes, always two decimals.

| currency | 123456789 | -500 |
|---|---|---|
| `USD` | `$1,234,567.89` | `-$5.00` |
| `GBP` | `£1,234,567.89` | `-£5.00` |
| `EUR` | `1.234.567,89 €` | `-5,00 €` |
| `CHF` | `CHF 1'234'567.89` | `CHF -5.00` |

Any other currency throws `new Error("unsupported currency: <code>")`.

`solution.test.ts` has a few examples.
