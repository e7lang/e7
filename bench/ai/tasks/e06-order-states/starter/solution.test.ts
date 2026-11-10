import { assert, assertEqual, assertThrows } from "e7:test";
import { Order, OrderState, nextState, replay } from "./solution.ts";

export function testHappyPath(): void {
  assertEqual(replay("A1", 2500, "pay@100,ship@200,deliver@300"), "A1: delivered (3 steps)");
}

export function testInvalidTransition(): void {
  assertEqual(replay("A2", 2500, "pay@100,deliver@200"), "error at 2: cannot deliver order in state paid");
  assertEqual(replay("A3", 2500, "pay@100,archive@200"), "error at 2: unknown event archive");
}

export function testOrderClass(): void {
  const o = new Order("A4", 990);
  o.apply("cancel", 50);
  assertEqual(o.state, OrderState.Cancelled);
  assert(!o.canApply("pay"));
  assertThrows(() => { o.apply("pay", 60); }, "cannot pay order in state cancelled");
  assertEqual(o.describe(), "A4: cancelled (1 step)");
}

export function testNextState(): void {
  assertEqual(nextState(OrderState.Paid, "ship"), OrderState.Shipped);
  assertEqual(nextState(OrderState.Shipped, "cancel"), null);
}
