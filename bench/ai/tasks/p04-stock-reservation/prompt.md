`original.ts` holds our stock reservation rules (safety buffer, backorders, all-or-nothing orders). We are moving
them into an E7 script so the warehouse app can run them offline, so please port it to E7 as `solution.ts`. The
app only calls `reserveStock`, so keep that function with the same signature and the same output for every
input; everything else can change as E7 needs.

```ts
export function reserveStock(stockJson: string, requestJson: string): string
```

`solution.test.ts` has two examples.
