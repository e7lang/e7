// A small generic LRU cache used by several scripts (price lookups, parsed configs, derived keys).

export interface CacheStats {
  hits: number;
  misses: number;
  evictions: number;
}

class Entry<K, V> {
  prev: Entry<K, V> | null = null;
  next: Entry<K, V> | null = null;
  constructor(readonly key: K, public value: V) {}
}

export class LruCache<K, V> {
  private map = new Map<K, Entry<K, V>>();
  private head: Entry<K, V> | null = null; // most recently used
  private tail: Entry<K, V> | null = null; // least recently used
  private hits = 0;
  private misses = 0;
  private evictions = 0;

  constructor(readonly capacity: number) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new Error("capacity must be a positive integer");
  }

  get size(): number {
    return this.map.size;
  }

  private unlink(e: Entry<K, V>): void {
    if (e.prev !== null) e.prev.next = e.next;
    else this.head = e.next;
    if (e.next !== null) e.next.prev = e.prev;
    else this.tail = e.prev;
    e.prev = null;
    e.next = null;
  }

  private pushFront(e: Entry<K, V>): void {
    e.next = this.head;
    if (this.head !== null) this.head.prev = e;
    this.head = e;
    if (this.tail === null) this.tail = e;
  }

  has(key: K): boolean {
    return this.map.has(key);
  }

  get(key: K): V | undefined {
    const e = this.map.get(key);
    if (e === undefined) {
      this.misses++;
      return undefined;
    }
    this.hits++;
    this.unlink(e);
    this.pushFront(e);
    return e.value;
  }

  set(key: K, value: V): void {
    const existing = this.map.get(key);
    if (existing !== undefined) {
      existing.value = value;
      this.unlink(existing);
      this.pushFront(existing);
      return;
    }
    const e = new Entry<K, V>(key, value);
    this.map.set(key, e);
    this.pushFront(e);
    while (this.map.size > this.capacity && this.tail !== null) {
      const victim = this.tail;
      this.unlink(victim);
      this.map.delete(victim.key);
      this.evictions++;
    }
  }

  delete(key: K): boolean {
    const e = this.map.get(key);
    if (e === undefined) return false;
    this.unlink(e);
    this.map.delete(key);
    return true;
  }

  // Keys from the most to the least recently used.
  keys(): K[] {
    const out: K[] = [];
    for (let e = this.head; e !== null; e = e.next) out.push(e.key);
    return out;
  }

  stats(): CacheStats {
    return { hits: this.hits, misses: this.misses, evictions: this.evictions };
  }
}

// Returns the cached value for `key`, computing and caching it on a miss.
export function getOrCompute<K, V>(cache: LruCache<K, V>, key: K, compute: (key: K) => V): V {
  const cached = cache.get(key);
  if (cached !== undefined) return cached;
  const value = compute(key);
  cache.set(key, value);
  return value;
}
