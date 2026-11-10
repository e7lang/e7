Our subscription billing logic is being shared with the mobile apps as an E7 script. All dates are calendar
dates in UTC written `YYYY-MM-DD`. Please write `solution.ts` with:

```ts
export interface Period { start: string; end: string }
export function addBusinessDays(date: string, days: number, holidays: string[]): string
export function billingDate(anchor: string, cycle: number): string
export function billingPeriod(anchor: string, cycle: number): Period
export function proratedCents(priceCents: number, period: Period, fromDate: string): number
```

- **Dates.** Every date argument (including `period.start`/`end`, but not the `holidays` entries, which are
  only compared as strings) must be exactly `YYYY-MM-DD` (4-2-2 digits) and a real calendar date; otherwise
  throw `new Error("invalid date: <the text>")`, e.g. for `2023-02-29`, `2024-1-05` or `2024-13-01`.
- **`addBusinessDays(date, days, holidays)`**: business days are Monday–Friday that are not in `holidays`.
  For `days > 0` step forward one calendar day at a time and count business days until `days` of them have
  passed (Friday + 1 → Monday); `days < 0` does the same backwards. `days === 0` returns `date` itself when it
  is a business day, otherwise the next business day after it.
- **`billingDate(anchor, cycle)`**: a subscription that started on `anchor` bills on the same day of month
  every month. Cycle 0 is the anchor itself, cycle `n` is `n` months later. If that month is too short, use its
  last day — always computed from the anchor, not from the previous billing date (anchor 2024-01-31: cycle 1 is
  2024-02-29, cycle 2 is 2024-03-31, cycle 13 is 2025-02-28). Cycles may be negative.
- **`billingPeriod(anchor, cycle)`**: `start` is `billingDate(anchor, cycle)`, `end` is the day before
  `billingDate(anchor, cycle + 1)`.
- **`proratedCents(priceCents, period, fromDate)`**: the part of the price for the days from `fromDate` to
  `period.end`, both inclusive, out of all days of the period (inclusive):
  `Math.round(priceCents * remainingDays / totalDays)`. A `fromDate` before `start` or after `end` throws
  `new Error("date outside period: <fromDate>")`.

Use `Date` with its UTC methods (`Date.UTC`, `getUTCDay`, ...). `solution.test.ts` has a few examples.
