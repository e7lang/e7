import { assertEqual, assertThrows } from "e7:test";
import { Bank, CheckingAccount, SavingsAccount } from "./solution.ts";

function bank(): Bank {
  const b = new Bank();
  b.add(new CheckingAccount("chk", 5000));
  b.add(new SavingsAccount("sav", 240));
  return b;
}

export function testTransferAndStatement(): void {
  const b = bank();
  b.get("chk").deposit(10000, "salary");
  b.transfer("chk", "sav", 2550);
  assertEqual(b.get("chk").statement(), "checking chk\nsalary: +100.00\ntransfer to sav: -25.50\nbalance: 74.50");
  assertEqual(b.get("sav").balance, 2550);
}

export function testOverdraftAndFee(): void {
  const b = bank();
  b.get("chk").withdraw(4000);
  assertThrows(() => { b.get("chk").withdraw(1001); }, "insufficient funds in chk");
  b.monthEnd();
  assertEqual(b.get("chk").balance, -5500);
}

export function testSavingsInterestAndLimit(): void {
  const b = bank();
  b.get("sav").deposit(100000);
  for (let i = 0; i < 3; i++) b.get("sav").withdraw(100);
  assertThrows(() => { b.get("sav").withdraw(100); }, "withdrawal limit reached for sav");
  b.monthEnd();
  assertEqual(b.get("sav").balance, 99899); // 99700 + round(99700 * 2.4% / 12)
}

export function testErrors(): void {
  const b = bank();
  assertThrows(() => { b.get("chk").deposit(0); }, "invalid amount");
  assertThrows(() => { b.transfer("chk", "nope", 100); }, "unknown account: nope");
  assertThrows(() => { b.add(new SavingsAccount("sav", 0)); }, "duplicate account: sav");
}
