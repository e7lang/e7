`solution.ts` is our offline sync (`syncAll` pulls pages through an async `fetchPage` the app passes in and
applies them to a `LocalStore`; `syncReport` makes the line for the sync screen). On bad mobile networks a
single failed request currently aborts the whole sync, and syncing collections one after another is slow.
Please extend `solution.ts`:

```ts
export interface RetryOptions {
  retries?: number;                       // extra attempts per page after a failed fetch, default 0
  sleep?: (ms: number) => Promise<void>;  // waits before a retry; default: retry without waiting
}

export async function syncAll(store: LocalStore, fetchPage: FetchPage, maxPages: number = 100, retry: RetryOptions = {}): Promise<SyncResult>
export async function syncReport(store: LocalStore, fetchPage: FetchPage, retry: RetryOptions = {}): Promise<string>
export async function syncCollections(stores: Map<string, LocalStore>, fetchers: Map<string, FetchPage>, retry: RetryOptions = {}): Promise<string[]>
```

**Retries**

- When `fetchPage(cursor)` rejects, call it again with the same cursor, up to `retries` more times for that
  page. Before the n-th retry of a page, `await sleep(200 * 2 ** (n - 1))` (200, 400, 800 ms, ...); the count
  starts again for every page. No sleep before the first attempt or after the last one.
- If every attempt for a page fails, `syncAll` rejects with the error of the last attempt (unchanged), so
  `syncReport` shows `sync failed: <its message>` as today.
- `syncAll` rejects with `invalid retries` (before fetching anything) unless `retries` is an integer >= 0.
- `maxPages` counts pages, not attempts, and keeps its `more than <n> pages` error.
- Without retry options everything behaves exactly as today (`SyncResult` and the report text do not change).

**Several collections**

- `syncCollections` syncs every collection in `fetchers` into the store with the same name in `stores`, all
  at the same time: start every collection's sync before awaiting any of them (with equally fast backends their
  page requests interleave). Use `maxPages` 100 and the given retry options.
- It returns one line per collection, sorted by name: `"<name>: <the syncReport text>"`, or
  `"<name>: no local store"` (without fetching) when `stores` has no store of that name. A failing
  collection does not affect the others.

`solution.test.ts` has the current tests (they show how to fake the backend with `later` / `laterReject`).
