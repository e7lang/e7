import { assertEqual, assertThrows } from "e7:test";
import { TokenBucket, RateLimiter } from "./solution.ts";

export function testStartsFullAndCapped(): void {
  const b = new TokenBucket(5, 10, 1000);
  assertEqual(b.available(1000), 5);
  assertEqual(b.isFull(1000), true);
  assertEqual(b.available(60000), 5);
  assertEqual(b.capacity, 5);
  assertEqual(b.refillPerSecond, 10);
}

export function testRefillIsContinuous(): void {
  const b = new TokenBucket(4, 2, 0); // one token per 500 ms
  assertEqual(b.tryRemove(0, 4), true);
  assertEqual(b.available(0), 0);
  assertEqual(b.available(250), 0);   // 0.5 token
  assertEqual(b.tryRemove(250), false);
  assertEqual(b.available(500), 1);
  assertEqual(b.available(1250), 2);  // 2.5 tokens
  assertEqual(b.tryRemove(1250, 2), true);
  assertEqual(b.retryAfterMs(1250), 250);  // 0.5 left, needs 0.5 more
  assertEqual(b.tryRemove(1500), true);
  assertEqual(b.isFull(1500), false);
  assertEqual(b.isFull(3500), true);
}

export function testFractionalRate(): void {
  const b = new TokenBucket(1, 0.5, 0); // one token every 2 s
  assertEqual(b.tryRemove(0), true);
  assertEqual(b.retryAfterMs(0), 2000);
  assertEqual(b.retryAfterMs(500), 1500);
  assertEqual(b.tryRemove(1500), false);
  assertEqual(b.tryRemove(2000), true);
}

export function testRetryAfterRoundsUp(): void {
  const b = new TokenBucket(3, 3, 0);
  b.tryRemove(0, 3);
  assertEqual(b.retryAfterMs(0), 334);       // 1000 / 3 = 333.33
  assertEqual(b.retryAfterMs(0, 3), 1000);
  assertEqual(b.retryAfterMs(0, 2), 667);
  assertEqual(b.available(0), 0);            // retryAfterMs takes nothing
}

export function testRetryAfterZeroWhenAvailable(): void {
  const b = new TokenBucket(10, 1, 0);
  assertEqual(b.retryAfterMs(0, 10), 0);
  assertEqual(b.tryRemove(0, 10), true);
  assertEqual(b.retryAfterMs(5000, 5), 0);
  assertEqual(b.retryAfterMs(5000, 6), 1000);
}

export function testFailedRemoveTakesNothing(): void {
  const b = new TokenBucket(5, 1, 0);
  assertEqual(b.tryRemove(0, 3), true);
  assertEqual(b.tryRemove(0, 3), false);
  assertEqual(b.available(0), 2);
  assertEqual(b.tryRemove(0, 2), true);
}

export function testClockGoingBackwards(): void {
  const b = new TokenBucket(2, 1, 10000);
  assertEqual(b.tryRemove(10000, 2), true);
  assertEqual(b.available(9000), 0);     // earlier: nothing added
  assertEqual(b.available(10500), 0);    // 0.5 since 10000
  // the jump back did not move the last time: at 11000 a full token has accrued since 10000
  assertEqual(b.tryRemove(5000), false);
  assertEqual(b.tryRemove(11000), true);
}

export function testInvalidArguments(): void {
  assertThrows(() => { new TokenBucket(0, 1, 0); }, "invalid bucket config");
  assertThrows(() => { new TokenBucket(2.5, 1, 0); }, "invalid bucket config");
  assertThrows(() => { new TokenBucket(5, 0, 0); }, "invalid bucket config");
  assertThrows(() => { new TokenBucket(5, -1, 0); }, "invalid bucket config");
  assertThrows(() => { new RateLimiter(5, 0); }, "invalid bucket config");
  const b = new TokenBucket(3, 1, 0);
  assertThrows(() => { b.tryRemove(0, 4); }, "invalid token count");
  assertThrows(() => { b.tryRemove(0, 0); }, "invalid token count");
  assertThrows(() => { b.tryRemove(0, 1.5); }, "invalid token count");
  assertThrows(() => { b.retryAfterMs(0, 4); }, "invalid token count");
  assertEqual(b.available(0), 3);
}

export function testLimiterPerKey(): void {
  const rl = new RateLimiter(3, 1);
  const got: boolean[] = [];
  for (let i = 0; i < 4; i++) got.push(rl.allow("a", 0));
  assertEqual(got, [true, true, true, false]);
  assertEqual(rl.allow("b", 0), true);
  assertEqual(rl.allow("a", 500), false);
  assertEqual(rl.allow("a", 1000), true);
  assertEqual(rl.size, 2);
}

export function testLimiterBucketStartsAtFirstUse(): void {
  const rl = new RateLimiter(1, 1);
  assertEqual(rl.allow("late", 50000), true);
  assertEqual(rl.allow("late", 50500), false);
  assertEqual(rl.allow("late", 51000), true);
}

export function testPrune(): void {
  const rl = new RateLimiter(2, 4); // empty -> full in 500 ms
  rl.allow("a", 0);
  rl.allow("a", 0);   // a: 0 tokens at 0, full at 500
  rl.allow("b", 250); // b: 1 token at 250, full at 500
  rl.allow("c", 375); // c: 1 token at 375, full at 625
  assertEqual(rl.size, 3);
  assertEqual(rl.prune(250), 0);
  assertEqual(rl.prune(500), 2);
  assertEqual(rl.size, 1);
  assertEqual(rl.prune(625), 1);
  assertEqual(rl.size, 0);
  assertEqual(rl.allow("a", 700), true);
  assertEqual(rl.size, 1);
}
