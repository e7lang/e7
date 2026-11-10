`original.ts` is our checkout calculation from the web app. We want the mobile app to run exactly the same
rules, so please port it to E7 as `solution.ts`. The app only calls `checkoutJson`, so keep that function
with the same signature and the same output for every input; everything else can change as E7 needs.

```ts
export function checkoutJson(cartJson: string, state: string): string
```

`solution.test.ts` has two examples.
