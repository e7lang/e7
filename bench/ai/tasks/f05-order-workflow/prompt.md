**Bug: refund accepted for an order that was never paid**

Finance found a refund for an order the customer never paid. `solution.ts` (E7) is our order workflow; the
allowed transitions are in the comment at the top of the file. A refund is only allowed for a cancelled or
delivered order that was paid; otherwise it must be rejected with `"nothing to refund"`.

It only happens when the shop has other orders. Steps to reproduce:

```ts
const book = new OrderBook();
book.open("A");
book.apply("A", "pay");     // "ok"
book.open("B");
book.apply("B", "cancel");  // "ok" (B was never paid)
book.apply("B", "refund");
```

Expected: `"nothing to refund"`, and B stays `"cancelled"`.
Actual: `"ok"`, and B becomes `"refunded"`.

Please fix `solution.ts`. The exported API stays as it is:

```ts
export type OrderStatus = "pending" | "paid" | "shipped" | "delivered" | "cancelled" | "refunded";
export type OrderEvent = "pay" | "ship" | "deliver" | "cancel" | "refund";
export class OrderBook {
  open(id: string): string                        // "ok" or "duplicate order <id>"
  status(id: string): string                      // "" for an unknown order
  history(id: string): string[]                   // "open", then every event that was applied
  apply(id: string, event: OrderEvent): string    // "ok" or the reason it was rejected
}
```

All other results (the transitions and their error messages) must stay as they are. `solution.test.ts` has the
existing tests.
