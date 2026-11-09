// A small generic LRU cache used by several scripts (price lookups, parsed configs, derived keys).

export interface CacheStats {
  hits: number;
  misses: number;
  evictions: number;
}

export interface CacheOptions<K, V> {
  maxWeight?: number; // default: no weight limit
  weigh?: (value: V) => number; // default: every entry weighs 1
  onEvict?: (key: K, value: V) => void; // called for entries evicted for capacity or weight
}

class Entry<K, V> {
  prev: Entry<K, V> | null = null;
  next: Entry<K, V> | null = null;
  constructor(readonly key: K, public value: V, public weight: number) {}
}

export class LruCache<K, V> {
  private map = new Map<K, Entry<K, V>>();
  private head: Entry<K, V> | null = null; // most recently used
  private tail: Entry<K, V> | null = null; // least recently used
  private hits = 0;
  private misses = 0;
  private evictions = 0;
  private totalWeight = 0;
  private readonly maxWeight: number;

  constructor(readonly capacity: number, private options: CacheOptions<K, V> = {}) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new Error("capacity must be a positive integer");
    this.maxWeight = options.maxWeight ?? Infinity;
    if (!(this.maxWeight > 0)) throw new Error("maxWeight must be positive");
  }

  get size(): number {
    return this.map.size;
  }

  get weight(): number {
    return this.totalWeight;
  }

  private weighValue(value: V): number {
    const weigh = this.options.weigh;
    if (weigh === undefined) return 1;
    const w = weigh(value);
    if (!Number.isFinite(w) || w < 0) throw new Error("invalid weight");
    return w;
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

  // Like get, without counting a hit or miss and without making the entry recently used.
  peek(key: K): V | undefined {
    return this.map.get(key)?.value;
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

  // Returns false (and changes nothing) when the value alone weighs more than maxWeight.
  set(key: K, value: V): boolean {
    const weight = this.weighValue(value);
    if (weight > this.maxWeight) return false;
    const existing = this.map.get(key);
    if (existing !== undefined) {
      this.totalWeight += weight - existing.weight;
      existing.value = value;
      existing.weight = weight;
      this.unlink(existing);
      this.pushFront(existing);
    } else {
      const e = new Entry<K, V>(key, value, weight);
      this.map.set(key, e);
      this.totalWeight += weight;
      this.pushFront(e);
    }
    while ((this.map.size > this.capacity || this.totalWeight > this.maxWeight) && this.tail !== null) {
      const victim = this.tail;
      this.remove(victim);
      this.evictions++;
      const onEvict = this.options.onEvict;
      if (onEvict !== undefined) onEvict(victim.key, victim.value);
    }
    return true;
  }

  private remove(e: Entry<K, V>): void {
    this.unlink(e);
    this.map.delete(e.key);
    this.totalWeight -= e.weight;
  }

  delete(key: K): boolean {
    const e = this.map.get(key);
    if (e === undefined) return false;
    this.remove(e);
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
