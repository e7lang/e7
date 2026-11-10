export class TokenBucket {
  readonly capacity: number;
  readonly refillPerSecond: number;
  #tokens: number;
  #lastMs: number;

  constructor(capacity: number, refillPerSecond: number, startMs: number) {
    if (!Number.isInteger(capacity) || capacity <= 0 || !(refillPerSecond > 0)) {
      throw new Error("invalid bucket config");
    }
    this.capacity = capacity;
    this.refillPerSecond = refillPerSecond;
    this.#tokens = capacity;
    this.#lastMs = startMs;
  }

  #refill(nowMs: number): void {
    if (nowMs <= this.#lastMs) return;
    const added = ((nowMs - this.#lastMs) * this.refillPerSecond) / 1000;
    this.#tokens = Math.min(this.capacity, this.#tokens + added);
    this.#lastMs = nowMs;
  }

  #check(tokens: number): void {
    if (!Number.isInteger(tokens) || tokens <= 0 || tokens > this.capacity) throw new Error("invalid token count");
  }

  tryRemove(nowMs: number, tokens: number = 1): boolean {
    this.#check(tokens);
    this.#refill(nowMs);
    if (this.#tokens < tokens) return false;
    this.#tokens -= tokens;
    return true;
  }

  available(nowMs: number): number {
    this.#refill(nowMs);
    return Math.floor(this.#tokens);
  }

  retryAfterMs(nowMs: number, tokens: number = 1): number {
    this.#check(tokens);
    this.#refill(nowMs);
    if (this.#tokens >= tokens) return 0;
    return Math.ceil(((tokens - this.#tokens) * 1000) / this.refillPerSecond);
  }

  isFull(nowMs: number): boolean {
    this.#refill(nowMs);
    return this.#tokens >= this.capacity;
  }
}

export class RateLimiter {
  #buckets = new Map<string, TokenBucket>();

  constructor(private capacity: number, private refillPerSecond: number) {
    if (!Number.isInteger(capacity) || capacity <= 0 || !(refillPerSecond > 0)) {
      throw new Error("invalid bucket config");
    }
  }

  get size(): number {
    return this.#buckets.size;
  }

  allow(key: string, nowMs: number): boolean {
    let bucket = this.#buckets.get(key);
    if (bucket === undefined) {
      bucket = new TokenBucket(this.capacity, this.refillPerSecond, nowMs);
      this.#buckets.set(key, bucket);
    }
    return bucket.tryRemove(nowMs);
  }

  prune(nowMs: number): number {
    let removed = 0;
    for (const [key, bucket] of [...this.#buckets]) {
      if (bucket.isFull(nowMs)) {
        this.#buckets.delete(key);
        removed++;
      }
    }
    return removed;
  }
}
