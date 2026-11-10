import { assertEqual } from "e7:test";
import { OrderBook } from "./solution.ts";

export function testHappyPath(): void {
  const book = new OrderBook();
  assertEqual(book.open("A-1001"), "ok");
  assertEqual(book.apply("A-1001", "pay"), "ok");
  assertEqual(book.apply("A-1001", "ship"), "ok");
  assertEqual(book.apply("A-1001", "deliver"), "ok");
  assertEqual(book.status("A-1001"), "delivered");
  assertEqual(book.history("A-1001"), ["open", "pay", "ship", "deliver"]);
}

export function testCannotShipUnpaid(): void {
  const book = new OrderBook();
  book.open("A-1002");
  assertEqual(book.apply("A-1002", "ship"), "cannot ship when pending");
  assertEqual(book.status("A-1002"), "pending");
}
