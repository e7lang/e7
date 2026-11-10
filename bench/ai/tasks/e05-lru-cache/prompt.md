`solution.ts` is the generic LRU cache several of our scripts share (`LruCache<K, V>` and `getOrCompute`). We
now want to cache parsed documents of very different sizes, so the cache needs a weight limit, and the
callers want to know when something is evicted. Please extend it:

```ts
export interface CacheOptions<K, V> {
  maxWeight?: number;                    // default: no weight limit
  weigh?: (value: V) => number;          // default: every entry weighs 1
  onEvict?: (key: K, value: V) => void;  // see below
}

// changed / new on LruCache<K, V>
constructor(capacity: number, options: CacheOptions<K, V> = {})
set(key: K, value: V): boolean   // was void
peek(key: K): V | undefined
get weight(): number             // total weight of the entries in the cache
```

Rules:

- The constructor still throws `capacity must be a positive integer` (checked first), and now also
  `maxWeight must be positive` when `maxWeight` is given and is not greater than 0.
- `set` weighs the value with `weigh`. If the result is not a finite number >= 0, it throws `invalid weight`
  and changes nothing. If the value alone weighs more than `maxWeight`, `set` returns `false` and changes
  nothing (an existing entry for the key keeps its old value and position). Otherwise it stores the value as
  today (an existing key gets the new value and weight and becomes the most recently used) and returns `true`.
- After storing, while there are more entries than `capacity` or the total weight is above `maxWeight`, the
  least recently used entry is evicted. The entry just set is never evicted (it always fits on its own).
- Every eviction counts in `stats().evictions`, and calls `onEvict(key, value)` once, after the entry has
  been removed (inside the callback the cache no longer has it and `size` / `weight` are already reduced),
  in eviction order. `delete` and overwriting a key are not evictions.
- `peek` returns the value like `get`, but does not count a hit or a miss and does not change the recency order.
- `getOrCompute` keeps its signature and behaviour; a computed value that is too heavy is returned but not cached.

Without options, everything must behave exactly as it does now. `solution.test.ts` has the current tests.
