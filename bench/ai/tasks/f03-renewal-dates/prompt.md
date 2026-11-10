**Bug: subscriptions started at the end of a month renew on the wrong day**

A customer who subscribed monthly on 2024-01-31 complains that we charge them on the 29th of every month.
`solution.ts` (E7) computes the billing schedule; the rule is in the comment at the top of the file: a
subscription renews on the day of the month it started, or on the last day of the month when the month is too
short for that day.

```ts
renewalDates("2024-01-31", 1, 4)
```

Expected: `["2024-02-29", "2024-03-31", "2024-04-30", "2024-05-31"]`
Actual: `["2024-02-29", "2024-03-29", "2024-04-29", "2024-05-29"]`

Billing support also says the refund we offered this customer when they cancelled on 2024-03-15 (price 31.00,
`refundCents(3100, "2024-01-31", 1, "2024-03-15")`) was 1497 cents; with the correct billing period
(2024-02-29 to 2024-03-31, 16 of its 31 days unused) it should be 1600.

Please fix `solution.ts`. The exported API stays as it is:

```ts
export function renewalDates(start: string, periodMonths: number, count: number): string[]
export function refundCents(priceCents: number, start: string, periodMonths: number, cancelDate: string): number
```

Everything else (date validation and its error messages, rounding of refunds, other periods such as quarterly
or yearly) must keep working as it does now. `solution.test.ts` has the existing tests.
