`original.ts` computes the shipping quotes shown at checkout (zones, weight tables, volumetric weight,
surcharges). The mobile app has to show exactly the same quotes before it talks to the server, so please port it
to E7 as `solution.ts`. The app only calls `quoteShipping`, so keep that function with the same signature and the
same output for every input; everything else can change as E7 needs.

```ts
export function quoteShipping(requestJson: string): string
```

`solution.test.ts` has two examples.
