import { assertEqual } from "e7:test";
import { later, laterReject } from "e7:test";
import { LocalStore, syncAll, syncReport } from "./solution.ts";
import type { Page } from "./solution.ts";

// A fake backend: page "p<n>" for cursor null / "c<n>".
function backend(pages: Page[]): (cursor: string | null) => Promise<Page> {
  return (cursor: string | null): Promise<Page> => {
    const index = cursor === null ? 0 : Number(cursor.slice(1));
    return later(pages[index]);
  };
}

const PAGES: Page[] = [
  { items: [{ id: "a", updatedAt: 1, data: "A" }, { id: "b", updatedAt: 1, data: "B" }], next: "c1" },
  { items: [{ id: "a", updatedAt: 2, deleted: true }, { id: "b", updatedAt: 1, data: "old" }], next: null },
];

export async function testSyncAll(): Promise<void> {
  const store = new LocalStore();
  const r = await syncAll(store, backend(PAGES));
  assertEqual(r, { pages: 2, upserted: 2, deleted: 1, skipped: 1 });
  assertEqual(store.ids(), ["b"]);
  assertEqual(store.get("b")?.data, "B");
}

export async function testReport(): Promise<void> {
  assertEqual(await syncReport(new LocalStore(), backend(PAGES)), "synced 2 pages: 2 upserted, 1 deleted, 1 skipped");
}

export async function testFailure(): Promise<void> {
  const failing = (cursor: string | null): Promise<Page> => laterReject<Page>(new Error("HTTP 503"));
  assertEqual(await syncReport(new LocalStore(), failing), "sync failed: HTTP 503");
}

export async function testPageLimit(): Promise<void> {
  const endless = (cursor: string | null): Promise<Page> => later<Page>({ items: [], next: "again" });
  let message = "";
  try {
    await syncAll(new LocalStore(), endless, 3);
  } catch (e) {
    if (e instanceof Error) message = e.message;
  }
  assertEqual(message, "more than 3 pages");
}
