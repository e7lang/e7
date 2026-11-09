import { assertEqual } from "e7:test";
import { OrderBook } from "./solution.ts";

export function testRefundOfUnpaidOrderWhileAnotherIsPaid(): void {
  const book = new OrderBook();
  book.open("A");
  assertEqual(book.apply("A", "pay"), "ok");
  book.open("B");
  assertEqual(book.apply("B", "cancel"), "ok");
  assertEqual(book.apply("B", "refund"), "nothing to refund");
  assertEqual(book.status("B"), "cancelled");
}

export function testHistoriesAreSeparate(): void {
  const book = new OrderBook();
  book.open("A");
  book.open("B");
  book.apply("A", "pay");
  book.apply("A", "ship");
  book.apply("B", "cancel");
  assertEqual(book.history("A"), ["open", "pay", "ship"]);
  assertEqual(book.history("B"), ["open", "cancel"]);
}

export function testNewOrderAfterOthers(): void {
  const book = new OrderBook();
  book.open("A");
  book.apply("A", "pay");
  book.apply("A", "cancel");
  book.apply("A", "refund");
  book.open("C");
  assertEqual(book.history("C"), ["open"]);
  assertEqual(book.status("C"), "pending");
}

export function testSeparateBooks(): void {
  const shop1 = new OrderBook();
  const shop2 = new OrderBook();
  shop1.open("X");
  shop1.apply("X", "pay");
  shop2.open("X");
  shop2.apply("X", "cancel");
  assertEqual(shop2.apply("X", "refund"), "nothing to refund");
  assertEqual(shop1.history("X"), ["open", "pay"]);
}

export function testRefundPaidCancelledAndDelivered(): void {
  const book = new OrderBook();
  book.open("A");
  book.apply("A", "pay");
  assertEqual(book.apply("A", "cancel"), "ok");
  assertEqual(book.apply("A", "refund"), "ok");
  assertEqual(book.status("A"), "refunded");
  assertEqual(book.apply("A", "refund"), "cannot refund when refunded");
}

export function testRefundAfterDelivery(): void {
  const book = new OrderBook();
  book.open("A");
  book.apply("A", "pay");
  book.apply("A", "ship");
  book.apply("A", "deliver");
  assertEqual(book.apply("A", "refund"), "ok");
  assertEqual(book.history("A"), ["open", "pay", "ship", "deliver", "refund"]);
}

export function testInvalidTransitions(): void {
  const book = new OrderBook();
  book.open("A");
  assertEqual(book.apply("A", "deliver"), "cannot deliver when pending");
  assertEqual(book.apply("A", "refund"), "cannot refund when pending");
  book.apply("A", "pay");
  assertEqual(book.apply("A", "pay"), "cannot pay when paid");
  book.apply("A", "ship");
  assertEqual(book.apply("A", "cancel"), "cannot cancel a shipped order");
  book.apply("A", "deliver");
  assertEqual(book.apply("A", "cancel"), "cannot cancel a shipped order");
  assertEqual(book.history("A"), ["open", "pay", "ship", "deliver"]);
}

export function testCancelledIsFinalExceptRefund(): void {
  const book = new OrderBook();
  book.open("A");
  book.apply("A", "cancel");
  assertEqual(book.apply("A", "pay"), "cannot pay when cancelled");
  assertEqual(book.apply("A", "cancel"), "cannot cancel when cancelled");
}

export function testUnknownAndDuplicate(): void {
  const book = new OrderBook();
  assertEqual(book.apply("nope", "pay"), "unknown order nope");
  assertEqual(book.status("nope"), "");
  const none: string[] = [];
  assertEqual(book.history("nope"), none);
  assertEqual(book.open("A"), "ok");
  assertEqual(book.open("A"), "duplicate order A");
}

export function testHistoryIsACopy(): void {
  const book = new OrderBook();
  book.open("A");
  book.history("A").push("pay");
  assertEqual(book.apply("A", "cancel"), "ok");
  assertEqual(book.apply("A", "refund"), "nothing to refund");
  assertEqual(book.history("A"), ["open", "cancel"]);
}
