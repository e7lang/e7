// Offline sync: pulls changed records page by page from the backend (through an async host function
// the app passes in) and applies them to the local store.

export interface Item {
  id: string;
  updatedAt: number;
  deleted?: boolean;
  data?: string;
}

export interface Page {
  items: Item[];
  next: string | null; // cursor of the next page, null on the last page
}

export type FetchPage = (cursor: string | null) => Promise<Page>;

export interface SyncResult {
  pages: number;
  upserted: number;
  deleted: number;
  skipped: number; // older than what the store already has
}

export interface RetryOptions {
  retries?: number; // extra attempts per page after a failed fetch (default 0)
  sleep?: (ms: number) => Promise<void>; // waits before a retry (default: no waiting)
}

const BASE_DELAY_MS = 200;

export type ApplyOutcome = "upserted" | "deleted" | "skipped";

export class LocalStore {
  private items = new Map<string, Item>();
  private tombstones = new Map<string, number>(); // id -> updatedAt of the deletion

  apply(item: Item): ApplyOutcome {
    const known = this.items.get(item.id)?.updatedAt ?? this.tombstones.get(item.id) ?? -Infinity;
    if (item.updatedAt <= known) return "skipped";
    if (item.deleted === true) {
      this.items.delete(item.id);
      this.tombstones.set(item.id, item.updatedAt);
      return "deleted";
    }
    this.tombstones.delete(item.id);
    this.items.set(item.id, item);
    return "upserted";
  }

  get(id: string): Item | null {
    return this.items.get(id) ?? null;
  }

  ids(): string[] {
    return [...this.items.keys()].sort();
  }
}

// Fetches one page, retrying a failed fetch up to `retries` times with exponential backoff
// (200 ms, 400 ms, 800 ms, ...). The last attempt's error is passed on.
async function fetchWithRetry(fetchPage: FetchPage, cursor: string | null, retry: RetryOptions): Promise<Page> {
  const retries = retry.retries ?? 0;
  for (let attempt = 0; ; attempt++) {
    try {
      return await fetchPage(cursor);
    } catch (e) {
      if (attempt >= retries) throw e;
    }
    const sleep = retry.sleep;
    if (sleep !== undefined) await sleep(BASE_DELAY_MS * 2 ** attempt);
  }
}

export async function syncAll(
  store: LocalStore,
  fetchPage: FetchPage,
  maxPages: number = 100,
  retry: RetryOptions = {},
): Promise<SyncResult> {
  const retries = retry.retries ?? 0;
  if (!Number.isInteger(retries) || retries < 0) throw new Error("invalid retries");
  const result: SyncResult = { pages: 0, upserted: 0, deleted: 0, skipped: 0 };
  let cursor: string | null = null;
  do {
    if (result.pages >= maxPages) throw new Error(`more than ${maxPages} pages`);
    const page = await fetchWithRetry(fetchPage, cursor, retry);
    result.pages++;
    for (const item of page.items) {
      const outcome = store.apply(item);
      switch (outcome) {
        case "upserted":
          result.upserted++;
          break;
        case "deleted":
          result.deleted++;
          break;
        case "skipped":
          result.skipped++;
          break;
      }
    }
    cursor = page.next;
  } while (cursor !== null);
  return result;
}

// One line for the sync screen.
export async function syncReport(store: LocalStore, fetchPage: FetchPage, retry: RetryOptions = {}): Promise<string> {
  try {
    const r = await syncAll(store, fetchPage, 100, retry);
    return `synced ${r.pages} ${r.pages === 1 ? "page" : "pages"}: ${r.upserted} upserted, ${r.deleted} deleted, ${r.skipped} skipped`;
  } catch (e) {
    return `sync failed: ${e instanceof Error ? e.message : String(e)}`;
  }
}

// Syncs every collection at the same time; one line per collection, sorted by name.
export async function syncCollections(
  stores: Map<string, LocalStore>,
  fetchers: Map<string, FetchPage>,
  retry: RetryOptions = {},
): Promise<string[]> {
  const names = [...fetchers.keys()].sort();
  const reports = names.map(async (name): Promise<string> => {
    const store = stores.get(name);
    const fetchPage = fetchers.get(name);
    if (store === undefined || fetchPage === undefined) return `${name}: no local store`;
    return `${name}: ${await syncReport(store, fetchPage, retry)}`;
  });
  return await Promise.all(reports);
}
