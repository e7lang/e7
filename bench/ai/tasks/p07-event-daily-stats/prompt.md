`original.ts` turns our raw event log into the per-day rows of the analytics dashboard (days in UTC). We want
the same numbers computed on the device from its local log, so please port it to E7 as `solution.ts`. The app
only calls `dailyStats`, so keep that function with the same signature and the same output for every input;
everything else can change as E7 needs.

```ts
export function dailyStats(log: string, fromDay: string, toDay: string): string
```

`solution.test.ts` has two examples.
