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

export async function syncAll(store: LocalStore, fetchPage: FetchPage, maxPages: number = 100): Promise<SyncResult> {
  const result: SyncResult = { pages: 0, upserted: 0, deleted: 0, skipped: 0 };
  let cursor: string | null = null;
  do {
    if (result.pages >= maxPages) throw new Error(`more than ${maxPages} pages`);
    const page = await fetchPage(cursor);
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
export async function syncReport(store: LocalStore, fetchPage: FetchPage): Promise<string> {
  try {
    const r = await syncAll(store, fetchPage);
    return `synced ${r.pages} ${r.pages === 1 ? "page" : "pages"}: ${r.upserted} upserted, ${r.deleted} deleted, ${r.skipped} skipped`;
  } catch (e) {
    return `sync failed: ${e instanceof Error ? e.message : String(e)}`;
  }
}
