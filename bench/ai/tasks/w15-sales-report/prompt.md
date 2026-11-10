The sales dashboard in the app should work offline from the cached orders export. Please write `solution.ts`
(an E7 script) that turns the export into the report the dashboard shows:

```ts
export function salesReport(ordersJson: string): string
```

The input is a JSON array of orders like:

```json
[{"id": "o1", "region": "EU", "customer": "c7", "status": "paid", "discountPercent": 10,
  "items": [{"sku": "pen", "qty": 3, "unitPriceCents": 250}, {"sku": "pad", "qty": 1, "unitPriceCents": 900}]}]
```

`discountPercent` is optional (0 when absent); other fields may appear and are ignored. The output is a JSON
string (no extra whitespace, keys in exactly this order):

```json
{"regions":[{"region":"EU","orders":1,"units":4,"revenueCents":1485,"customers":1,"topSku":"pen"}],"totalRevenueCents":1485,"skipped":0}
```

Rules:

- Only orders with `status` `"paid"` count; every other order only adds 1 to `skipped`.
- One row per region (region names are compared exactly), for regions with at least one paid order:
  - `orders`: number of paid orders; `units`: sum of `qty`;
  - `revenueCents`: sum over its orders of `Math.round(subtotal * (100 - discountPercent) / 100)` where
    `subtotal` is the order's sum of `qty * unitPriceCents` (rounded per order, exactly this formula);
  - `customers`: number of distinct `customer` values;
  - `topSku`: the SKU with the most units in the region (adding up all its items across orders); on a tie the
    alphabetically smallest (plain `<` string order); `""` if the region sold no units.
- Rows are sorted by `revenueCents` descending, then by region name ascending (`<` order).
- `totalRevenueCents` is the sum of the rows' `revenueCents`.
- An order with no items still counts as an order (and a customer).
- If `ordersJson` is not valid JSON, return exactly `{"error":"invalid json"}`.

`solution.test.ts` has a couple of examples.
