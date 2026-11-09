import { assertEqual } from "e7:test";
import { later, laterReject } from "e7:test";
import { LocalStore, syncAll, syncCollections, syncReport } from "./solution.ts";
import type { FetchPage, Page } from "./solution.ts";

const PAGES: Page[] = [
  { items: [{ id: "a", updatedAt: 1, data: "A" }, { id: "b", updatedAt: 1, data: "B" }], next: "c1" },
  { items: [{ id: "a", updatedAt: 2, deleted: true }, { id: "b", updatedAt: 1, data: "old" }], next: "c2" },
  { items: [{ id: "a", updatedAt: 1, data: "stale" }, { id: "c", updatedAt: 5, data: "C" }], next: null },
];

// A fake backend that records every call and fails the calls listed in `failures`
// (as "<cursor>#<attempt>", attempts counted per cursor from 1).
class Backend {
  calls: string[] = [];
  private attempts = new Map<string, number>();
  constructor(private pages: Page[], private failures: string[] = [], private tag: string = "") {}

  fetch(cursor: string | null): Promise<Page> {
    const key = cursor ?? "start";
    const attempt = (this.attempts.get(key) ?? 0) + 1;
    this.attempts.set(key, attempt);
    this.calls.push(`${this.tag}${key}`);
    if (this.failures.includes(`${key}#${attempt}`)) return laterReject<Page>(new Error(`HTTP 503 on ${key} attempt ${attempt}`));
    const index = cursor === null ? 0 : Number(cursor.slice(1));
    return later(this.pages[index]);
  }

  fetcher(): FetchPage {
    return (cursor: string | null): Promise<Page> => this.fetch(cursor);
  }
}

function sleeper(log: number[]): (ms: number) => Promise<void> {
  return async (ms: number): Promise<void> => {
    log.push(ms);
    await later(0);
  };
}

async function errorOf(p: Promise<string>): Promise<string> {
  try {
    await p;
    return "no error";
  } catch (e) {
    return e instanceof Error ? e.message : String(e);
  }
}

export async function testSyncUnchangedWithoutRetries(): Promise<void> {
  const store = new LocalStore();
  const r = await syncAll(store, new Backend(PAGES).fetcher());
  assertEqual(r, { pages: 3, upserted: 3, deleted: 1, skipped: 2 });
  assertEqual(store.ids(), ["b", "c"]);
  const b = new Backend(PAGES, ["c1#1"]);
  assertEqual(await syncReport(new LocalStore(), b.fetcher()), "sync failed: HTTP 503 on c1 attempt 1");
  assertEqual(b.calls, ["start", "c1"]);
}

export async function testRetrySucceeds(): Promise<void> {
  const b = new Backend(PAGES, ["start#1", "start#2"]);
  const sleeps: number[] = [];
  const r = await syncAll(new LocalStore(), b.fetcher(), 100, { retries: 2, sleep: sleeper(sleeps) });
  assertEqual(r, { pages: 3, upserted: 3, deleted: 1, skipped: 2 });
  assertEqual(b.calls, ["start", "start", "start", "c1", "c2"]);
  assertEqual(sleeps, [200, 400]);
}

export async function testRetriesExhausted(): Promise<void> {
  const b = new Backend(PAGES, ["c1#1", "c1#2", "c1#3", "c1#4"]);
  const sleeps: number[] = [];
  const report = await syncReport(new LocalStore(), b.fetcher(), { retries: 3, sleep: sleeper(sleeps) });
  assertEqual(report, "sync failed: HTTP 503 on c1 attempt 4");
  assertEqual(b.calls, ["start", "c1", "c1", "c1", "c1"]);
  assertEqual(sleeps, [200, 400, 800]);
}

export async function testBackoffRestartsForEachPage(): Promise<void> {
  const b = new Backend(PAGES, ["start#1", "c2#1", "c2#2"]);
  const sleeps: number[] = [];
  const report = await syncReport(new LocalStore(), b.fetcher(), { retries: 2, sleep: sleeper(sleeps) });
  assertEqual(report, "synced 3 pages: 3 upserted, 1 deleted, 2 skipped");
  assertEqual(sleeps, [200, 200, 400]);
}

export async function testRetriesWithoutSleep(): Promise<void> {
  const b = new Backend(PAGES, ["c1#1"]);
  const r = await syncAll(new LocalStore(), b.fetcher(), 100, { retries: 1 });
  assertEqual(r.pages, 3);
  assertEqual(b.calls, ["start", "c1", "c1", "c2"]);
}

export async function testZeroRetriesDoesNotSleep(): Promise<void> {
  const sleeps: number[] = [];
  const b = new Backend(PAGES, ["start#1"]);
  const msg = await errorOf(syncAll(new LocalStore(), b.fetcher(), 100, { retries: 0, sleep: sleeper(sleeps) }).then((r) => `${r.pages}`));
  assertEqual(msg, "HTTP 503 on start attempt 1");
  assertEqual(sleeps, []);
}

export async function testInvalidRetries(): Promise<void> {
  const b = new Backend(PAGES);
  assertEqual(await errorOf(syncAll(new LocalStore(), b.fetcher(), 100, { retries: -1 }).then((r) => "")), "invalid retries");
  assertEqual(await errorOf(syncAll(new LocalStore(), b.fetcher(), 100, { retries: 1.5 }).then((r) => "")), "invalid retries");
  assertEqual(b.calls, []);
}

export async function testPageLimitStillApplies(): Promise<void> {
  const endless = (cursor: string | null): Promise<Page> => later<Page>({ items: [], next: "again" });
  assertEqual(await errorOf(syncAll(new LocalStore(), endless, 2, { retries: 3 }).then((r) => "")), "more than 2 pages");
}

export async function testSyncCollectionsRunConcurrently(): Promise<void> {
  const a = new Backend(PAGES, [], "a:");
  const b = new Backend(PAGES, [], "b:");
  const stores = new Map<string, LocalStore>([["b", new LocalStore()], ["a", new LocalStore()]]);
  const fetchers = new Map<string, FetchPage>([["b", b.fetcher()], ["a", a.fetcher()]]);
  const log: string[] = [];
  const both = new Map<string, FetchPage>([
    ["b", (c: string | null): Promise<Page> => { log.push(`b:${c ?? "start"}`); return b.fetch(c); }],
    ["a", (c: string | null): Promise<Page> => { log.push(`a:${c ?? "start"}`); return a.fetch(c); }],
  ]);
  const lines = await syncCollections(stores, both);
  assertEqual(lines, [
    "a: synced 3 pages: 3 upserted, 1 deleted, 2 skipped",
    "b: synced 3 pages: 3 upserted, 1 deleted, 2 skipped",
  ]);
  assertEqual(log, ["a:start", "b:start", "a:c1", "b:c1", "a:c2", "b:c2"]);
  assertEqual(stores.get("a")?.ids(), ["b", "c"]);
  assertEqual(fetchers.size, 2);
}

export async function testSyncCollectionsFailuresAndMissingStores(): Promise<void> {
  const ok = new Backend(PAGES);
  const bad = new Backend(PAGES, ["c1#1", "c1#2"]);
  const stores = new Map<string, LocalStore>([["orders", new LocalStore()], ["notes", new LocalStore()]]);
  const fetchers = new Map<string, FetchPage>([
    ["orders", bad.fetcher()],
    ["notes", ok.fetcher()],
    ["contacts", ok.fetcher()],
  ]);
  assertEqual(await syncCollections(stores, fetchers), [
    "contacts: no local store",
    "notes: synced 3 pages: 3 upserted, 1 deleted, 2 skipped",
    "orders: sync failed: HTTP 503 on c1 attempt 1",
  ]);
}

export async function testSyncCollectionsWithRetries(): Promise<void> {
  const bad = new Backend(PAGES, ["c1#1", "c1#2"]);
  const sleeps: number[] = [];
  const stores = new Map<string, LocalStore>([["orders", new LocalStore()]]);
  const fetchers = new Map<string, FetchPage>([["orders", bad.fetcher()]]);
  assertEqual(await syncCollections(stores, fetchers, { retries: 2, sleep: sleeper(sleeps) }), [
    "orders: synced 3 pages: 3 upserted, 1 deleted, 2 skipped",
  ]);
  assertEqual(sleeps, [200, 400]);
  assertEqual(await syncCollections(new Map<string, LocalStore>(), new Map<string, FetchPage>()), []);
}
