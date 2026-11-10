import { assert, assertEqual, assertThrows } from "e7:test";
import { Order, OrderState, allowedEvents, nextState, replay } from "./solution.ts";

const DAY = 86400;

export function testExistingFlowsUnchanged(): void {
  assertEqual(replay("A1", 2500, "pay@100,ship@200,deliver@300"), "A1: delivered (3 steps)");
  assertEqual(replay("A2", 2500, "pay@100,deliver@200"), "error at 2: cannot deliver order in state paid");
  assertEqual(replay("A3", 2500, " cancel@5 "), "A3: cancelled (1 step)");
  assertEqual(replay("A4", 2500, ""), "A4: created (0 steps)");
  assertEqual(replay("A5", 2500, "pay@100,ship@50"), "error at 2: event out of order: ship@50");
  assertEqual(replay("A6", 2500, "pay@x"), "error at 1: bad time for pay");
  assertEqual(replay("A7", 2500, "pay@1,archive@2"), "error at 2: unknown event archive");
}

export function testReturnAndRefund(): void {
  assertEqual(replay("B1", 4999, `pay@100,ship@200,deliver@300,return@${300 + 3 * DAY},refund@${300 + 5 * DAY}`), "B1: refunded 49.99 (5 steps)");
  assertEqual(replay("B2", 4999, "pay@100,ship@200,deliver@300,return@400"), "B2: returned (4 steps)");
}

export function testReturnWindowBoundary(): void {
  const base = "pay@100,ship@200,deliver@1000";
  assertEqual(replay("C1", 100, `${base},return@${1000 + 30 * DAY}`), "C1: returned (4 steps)");
  assertEqual(replay("C2", 100, `${base},return@${1000 + 30 * DAY + 1}`), "error at 4: return window closed");
}

export function testCannotReturnBeforeDelivery(): void {
  assertEqual(replay("D1", 100, "pay@100,ship@200,return@300"), "error at 3: cannot return order in state shipped");
  assertEqual(replay("D2", 100, "pay@100,ship@200,deliver@300,return@400,return@500"), "error at 5: cannot return order in state returned");
}

export function testRefundAfterPaidCancellation(): void {
  assertEqual(replay("E1", 1205, "pay@100,cancel@200,refund@300"), "E1: refunded 12.05 (3 steps)");
  assertEqual(replay("E2", 1205, "cancel@200,refund@300"), "error at 2: nothing to refund");
  assertEqual(replay("E3", 1205, "pay@100,refund@200"), "error at 2: cannot refund order in state paid");
}

export function testRefundedIsFinal(): void {
  const o = new Order("F1", 700);
  o.apply("pay", 1);
  o.apply("cancel", 2);
  o.apply("refund", 3);
  assertEqual(o.state, OrderState.Refunded);
  for (const e of ["pay", "ship", "deliver", "cancel", "return", "refund"]) {
    assertEqual(replay("F2", 700, `pay@1,cancel@2,refund@3,${e}@4`), `error at 4: cannot ${e} order in state refunded`);
  }
  assertThrows(() => { o.apply("refund", 4); }, "cannot refund order in state refunded");
}

export function testRefundedCents(): void {
  const o = new Order("G1", 3050);
  assertEqual(o.refundedCents, 0);
  o.apply("pay", 1);
  o.apply("ship", 2);
  o.apply("deliver", 3);
  o.apply("return", 4);
  assertEqual(o.refundedCents, 0);
  o.apply("refund", 5);
  assertEqual(o.refundedCents, 3050);
  assertEqual(o.describe(), "G1: refunded 30.50 (5 steps)");
  assertEqual(new Order("G2", 7).describe(), "G2: created (0 steps)");
}

export function testNextStateTable(): void {
  assertEqual(nextState(OrderState.Delivered, "return"), OrderState.Returned);
  assertEqual(nextState(OrderState.Returned, "refund"), OrderState.Refunded);
  assertEqual(nextState(OrderState.Cancelled, "refund"), OrderState.Refunded);
  assertEqual(nextState(OrderState.Paid, "refund"), null);
  assertEqual(nextState(OrderState.Delivered, "cancel"), null);
  assertEqual(nextState(OrderState.Refunded, "refund"), null);
  assertEqual(nextState(OrderState.Created, "pay"), OrderState.Paid);
}

export function testAllowedEvents(): void {
  assertEqual(allowedEvents(OrderState.Created), ["pay", "cancel"]);
  assertEqual(allowedEvents(OrderState.Paid), ["ship", "cancel"]);
  assertEqual(allowedEvents(OrderState.Shipped), ["deliver"]);
  assertEqual(allowedEvents(OrderState.Delivered), ["return"]);
  assertEqual(allowedEvents(OrderState.Cancelled), ["refund"]);
  assertEqual(allowedEvents(OrderState.Returned), ["refund"]);
  assertEqual(allowedEvents(OrderState.Refunded), []);
}

export function testCanApplyUsesTable(): void {
  const o = new Order("H1", 100);
  o.apply("cancel", 1);
  assert(o.canApply("refund")); // the table allows it; apply() then says "nothing to refund"
  assertThrows(() => { o.apply("refund", 2); }, "nothing to refund");
  assertEqual(o.history.length, 1);
}

export function testCheckOrder(): void {
  // out of order is reported before an invalid transition and before the return window
  assertEqual(replay("I1", 100, `pay@100,ship@200,deliver@${40 * DAY},return@10`), "error at 4: event out of order: return@10");
  assertEqual(replay("I2", 100, "pay@100,ship@200,return@100"), "error at 3: event out of order: return@100");
}

export function testHistoryRecordsNewEvents(): void {
  const o = new Order("J1", 100);
  o.apply("pay", 1);
  o.apply("cancel", 2);
  o.apply("refund", 2);
  assertEqual(o.history[2], { event: "refund", from: OrderState.Cancelled, to: OrderState.Refunded, at: 2 });
}
