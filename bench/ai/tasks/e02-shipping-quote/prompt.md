`solution.ts` computes shipping quotes in the checkout (`quoteShipping(configJson, orderJson)`; the rate table
comes from the backend as JSON). We are launching express shipping and a surcharge for oversize items. Please
extend `solution.ts`; the exported function keeps its signature and result format:

```ts
export function quoteShipping(configJson: string, orderJson: string): string
```

New, all optional, JSON fields (old configurations and orders without them must keep giving exactly the same
results as today):

- zone: `expressMultiplier` (number). Express is offered only in zones that have it.
- zone: `oversizeCents` (number, default 0): a surcharge per oversize unit.
- order item: `oversize` (boolean, default false).
- order: `service` (string): `"standard"` (the default when missing) or `"express"`, exactly these spellings.

Pricing:

- Standard: as today (`baseCents + perKgCents * (weightKg - 1)`, free when the merchandise subtotal reaches
  `freeOverCents`).
- Express: `Math.round((baseCents + perKgCents * (weightKg - 1)) * expressMultiplier)`; the free-shipping
  threshold does not apply to express.
- Then, for both services, add `oversizeCents` for every oversize unit (an item with `oversize: true` and
  `qty: 2` is two units). The surcharge is charged even when standard shipping is free.

Errors are returned as `{"error":"..."}` like the existing ones. When several apply, report the first in this order:

1. `empty order`
2. `unknown service: <service>` (the value as given)
3. `no zone for <COUNTRY>` (as today: trimmed and uppercased)
4. `express not available to <COUNTRY>` (express requested and the zone has no `expressMultiplier`)
5. `bad quantity for <sku>`

`invalid config` / `invalid order` for unparsable JSON stay as they are. `solution.test.ts` has the current tests.
