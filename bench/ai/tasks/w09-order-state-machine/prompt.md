Orders in the field app need to follow the same lifecycle rules as the backend even while offline, so we want
the state machine as an E7 class. Please write `solution.ts` with:

```ts
export interface LogEntry { at: number; event: string; from: string; to: string }
export class Order {
  readonly id: string;
  constructor(id: string, createdAt: number);
  get state(): string;
  get history(): LogEntry[];
  allowedEvents(): string[];
  apply(event: string, at: number): void;
}
```

A new order is `"pending"`. Times are Unix seconds. The allowed transitions are:

| state | event | new state |
|---|---|---|
| pending | pay | paid |
| pending | cancel | cancelled |
| paid | ship | shipped |
| paid | cancel | refunded |
| shipped | deliver | delivered |
| delivered | return | returned |
| returned | refund | refunded |

`cancelled` and `refunded` are final.

`apply(event, at)` performs a transition and records it in the log. If it cannot, it throws an `Error` and
changes nothing. Checks, in this order:

1. not one of the six events above → `unknown event: <event>`
2. `at` is earlier than the creation time or the time of the last accepted event (equal is fine) →
   `time went backwards`
3. no transition for this event from the current state → `cannot <event> an order that is <state>`
   (e.g. `cannot ship an order that is pending`)
4. `return` more than 30 days (2,592,000 seconds) after the `deliver` event → `return window closed`
   (exactly 30 days is still fine)

`history` returns the accepted transitions, oldest first, as `{ at, event, from, to }`; it returns a copy, so
changing the returned array or its entries must not affect the order. `allowedEvents()` lists the events that
have a transition from the current state (ignoring the return window), in the order
`pay, ship, deliver, cancel, return, refund`; it is `[]` in a final state.

`solution.test.ts` has a couple of examples.
