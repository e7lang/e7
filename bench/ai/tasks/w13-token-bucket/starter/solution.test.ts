import { assertEqual } from "e7:test";
import { TokenBucket, RateLimiter } from "./solution.ts";

export function testBucket(): void {
  const b = new TokenBucket(2, 1, 0); // 2 tokens, 1 per second
  assertEqual(b.tryRemove(0), true);
  assertEqual(b.tryRemove(0), true);
  assertEqual(b.tryRemove(0), false);
  assertEqual(b.retryAfterMs(0), 1000);
  assertEqual(b.tryRemove(1000), true);
}

export function testLimiter(): void {
  const rl = new RateLimiter(1, 0.5);
  assertEqual(rl.allow("alice", 0), true);
  assertEqual(rl.allow("alice", 1000), false);
  assertEqual(rl.allow("bob", 1000), true);
  assertEqual(rl.size, 2);
}
