`solution.ts` is the order state machine behind the "My orders" screen (`OrderState`, `nextState`, the
`Order` class and `replay`, which replays the event list we get from the backend). We are launching returns,
so the backend will start sending two new events. Please extend `solution.ts`:

```ts
export enum OrderState {
  Created = "created", Paid = "paid", Shipped = "shipped", Delivered = "delivered", Cancelled = "cancelled",
  Returned = "returned",   // new
  Refunded = "refunded",   // new
}
export type OrderEvent = "pay" | "ship" | "deliver" | "cancel" | "return" | "refund";

export function allowedEvents(state: OrderState): OrderEvent[]   // new

// new on Order
get refundedCents(): number
```

Rules:

- New transitions in `nextState`: delivered + `return` -> returned; returned + `refund` -> refunded;
  cancelled + `refund` -> refunded. Refunded accepts no event. Everything else stays as it is (for example
  paid + `refund` is not allowed: `cannot refund order in state paid`).
- `Order.apply` adds two rules, checked after the existing ones (out-of-order time, then
  `cannot <event> order in state <state>`):
  - `return` more than 30 days (30 × 86400 seconds) after the `deliver` event throws `return window closed`
    (exactly 30 days is still fine);
  - `refund` of a cancelled order that was never paid throws `nothing to refund`.
  A failed `apply` changes nothing.
- `canApply` keeps answering from the transition table only (`nextState(...) !== null`).
- `allowedEvents(state)`: the events `nextState` accepts in that state, in the order
  `pay, ship, deliver, cancel, return, refund`.
- `refundedCents`: `totalCents` once the order is refunded, otherwise 0.
- `describe()` of a refunded order shows the amount after the state: `B1: refunded 49.99 (5 steps)` (cents as
  `<whole>.<two digits>`). Other states are described as today.
- `replay` accepts the new event names; its error format stays `error at <n>: <message>`.

`solution.test.ts` has the current tests.
