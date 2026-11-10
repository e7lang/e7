import { assertEqual, assertThrows } from "e7:test";
import { Order } from "./solution.ts";

const DAY = 86400;

function errorOf(o: Order, event: string, at: number): string {
  try {
    o.apply(event, at);
  } catch (e) {
    if (e instanceof Error) return e.message;
  }
  return "ok";
}

function delivered(at: number): Order {
  const o = new Order("D", at - 300);
  o.apply("pay", at - 200);
  o.apply("ship", at - 100);
  o.apply("deliver", at);
  return o;
}

export function testNewOrder(): void {
  const o = new Order("N-1", 500);
  assertEqual(o.id, "N-1");
  assertEqual(o.state, "pending");
  assertEqual(o.history, []);
  assertEqual(o.allowedEvents(), ["pay", "cancel"]);
}

export function testFullReturnFlow(): void {
  const o = delivered(10000);
  o.apply("return", 10000 + 5 * DAY);
  o.apply("refund", 10000 + 6 * DAY);
  assertEqual(o.state, "refunded");
  assertEqual(o.history.map((e) => `${e.event}:${e.from}->${e.to}`), [
    "pay:pending->paid", "ship:paid->shipped", "deliver:shipped->delivered",
    "return:delivered->returned", "refund:returned->refunded",
  ]);
  assertEqual(o.history[4], { at: 10000 + 6 * DAY, event: "refund", from: "returned", to: "refunded" });
}

export function testCancelPendingAndPaid(): void {
  const a = new Order("C-1", 0);
  a.apply("cancel", 5);
  assertEqual(a.state, "cancelled");
  const b = new Order("C-2", 0);
  b.apply("pay", 5);
  b.apply("cancel", 6);
  assertEqual(b.state, "refunded");
  assertEqual(b.history[1], { at: 6, event: "cancel", from: "paid", to: "refunded" });
}

export function testFinalStates(): void {
  const a = new Order("F-1", 0);
  a.apply("cancel", 1);
  assertEqual(a.allowedEvents(), []);
  for (const e of ["pay", "ship", "deliver", "cancel", "return", "refund"]) {
    assertEqual(errorOf(a, e, 2), `cannot ${e} an order that is cancelled`);
  }
  const b = new Order("F-2", 0);
  b.apply("pay", 1);
  b.apply("cancel", 2);
  assertEqual(b.allowedEvents(), []);
  assertEqual(errorOf(b, "refund", 3), "cannot refund an order that is refunded");
}

export function testAllowedEventsPerState(): void {
  const o = new Order("S", 0);
  o.apply("pay", 1);
  assertEqual(o.allowedEvents(), ["ship", "cancel"]);
  o.apply("ship", 2);
  assertEqual(o.allowedEvents(), ["deliver"]);
  o.apply("deliver", 3);
  assertEqual(o.allowedEvents(), ["return"]);
  o.apply("return", 3);
  assertEqual(o.allowedEvents(), ["refund"]);
}

export function testInvalidTransitions(): void {
  const o = new Order("I", 0);
  assertEqual(errorOf(o, "deliver", 1), "cannot deliver an order that is pending");
  assertEqual(errorOf(o, "refund", 1), "cannot refund an order that is pending");
  o.apply("pay", 1);
  assertEqual(errorOf(o, "pay", 2), "cannot pay an order that is paid");
  assertEqual(errorOf(o, "return", 2), "cannot return an order that is paid");
  o.apply("ship", 2);
  assertEqual(errorOf(o, "cancel", 3), "cannot cancel an order that is shipped");
  assertEqual(o.state, "shipped");
  assertEqual(o.history.length, 2);
}

export function testUnknownEvent(): void {
  const o = new Order("U", 100);
  assertEqual(errorOf(o, "Pay", 101), "unknown event: Pay");
  assertEqual(errorOf(o, "", 101), "unknown event: ");
  // unknown is reported before a bad time
  assertEqual(errorOf(o, "explode", 1), "unknown event: explode");
}

export function testTimeOrdering(): void {
  const o = new Order("T", 100);
  assertEqual(errorOf(o, "pay", 99), "time went backwards");
  assertEqual(errorOf(o, "pay", 100), "ok");
  assertEqual(errorOf(o, "ship", 100), "ok");
  assertEqual(errorOf(o, "deliver", 50), "time went backwards");
  // time is checked before the transition
  assertEqual(errorOf(o, "refund", 50), "time went backwards");
  // a rejected event does not move the clock
  assertEqual(errorOf(o, "refund", 500), "cannot refund an order that is shipped");
  assertEqual(errorOf(o, "deliver", 200), "ok");
}

export function testReturnWindow(): void {
  const t = 1700000000;
  assertEqual(errorOf(delivered(t), "return", t + 30 * DAY), "ok");
  assertEqual(errorOf(delivered(t), "return", t + 30 * DAY + 1), "return window closed");
  const o = delivered(t);
  assertEqual(errorOf(o, "return", t + 40 * DAY), "return window closed");
  assertEqual(o.state, "delivered");
  assertEqual(o.history.length, 3);
}

export function testHistoryIsACopy(): void {
  const o = new Order("H", 0);
  o.apply("pay", 1);
  const h = o.history;
  h.push({ at: 2, event: "ship", from: "paid", to: "shipped" });
  h[0].to = "hacked";
  assertEqual(o.history, [{ at: 1, event: "pay", from: "pending", to: "paid" }]);
  assertEqual(o.state, "paid");
}

export function testIndependentOrders(): void {
  const a = new Order("A", 0);
  const b = new Order("B", 0);
  a.apply("pay", 1);
  assertEqual(b.state, "pending");
  assertEqual(b.history, []);
}
