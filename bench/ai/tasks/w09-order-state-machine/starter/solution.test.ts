import { assertEqual, assertThrows } from "e7:test";
import { Order } from "./solution.ts";

export function testHappyPath(): void {
  const o = new Order("A-1", 1000);
  o.apply("pay", 1010);
  o.apply("ship", 2000);
  assertEqual(o.state, "shipped");
  assertEqual(o.history, [
    { at: 1010, event: "pay", from: "pending", to: "paid" },
    { at: 2000, event: "ship", from: "paid", to: "shipped" },
  ]);
}

export function testRejected(): void {
  const o = new Order("A-2", 1000);
  assertThrows(() => { o.apply("ship", 1001); }, "cannot ship an order that is pending");
  assertEqual(o.allowedEvents(), ["pay", "cancel"]);
}
