import { assert, assertEqual, assertThrows } from "e7:test";
import { Bank, CheckingAccount, CreditAccount, SavingsAccount, formatCents } from "./solution.ts";

function bank(): Bank {
  const b = new Bank();
  b.add(new CheckingAccount("chk", 5000));
  b.add(new SavingsAccount("sav", 240));
  b.add(new CreditAccount("card", 100000, 1999));
  return b;
}

export function testExistingAccountsUnchanged(): void {
  const b = bank();
  b.get("chk").deposit(10000, "salary");
  b.transfer("chk", "sav", 2550);
  b.get("chk").withdraw(12000, "rent");
  b.monthEnd();
  assertEqual(b.get("chk").statement(), "checking chk\nsalary: +100.00\ntransfer to sav: -25.50\nrent: -120.00\noverdraft fee: -15.00\nbalance: -60.50");
  assertEqual(b.get("sav").statement(), "savings sav\ntransfer from chk: +25.50\ninterest: +0.05\nbalance: 25.55");
  assertEqual(formatCents(-5), "-0.05");
}

export function testCreditSpendingWithinLimit(): void {
  const b = bank();
  const card = b.get("card");
  card.withdraw(60000, "laptop");
  card.withdraw(40000, "phone");
  assertEqual(card.balance, -100000);
  assertThrows(() => { card.withdraw(1); }, "insufficient funds in card");
  assertEqual(card.kind, "credit");
}

export function testCreditInterest(): void {
  const b = bank();
  b.get("card").withdraw(33333, "tickets");
  b.monthEnd();
  // round(33333 * 19.99% / 12) = 555
  assertEqual(b.get("card").balance, -33888);
  assertEqual(b.get("card").statement(), "credit card\ntickets: -333.33\ninterest: -5.55\nbalance: -338.88");
  // interest is charged even when it takes the balance past the limit
  const c = new CreditAccount("c2", 1000, 1500);
  c.withdraw(1000);
  c.monthEnd();
  assertEqual(c.balance, -1013); // 12.5 rounds to 13
}

export function testCreditNoInterestWhenNothingOwed(): void {
  const c = new CreditAccount("c", 50000, 2400);
  c.monthEnd();
  c.withdraw(1000);
  c.deposit(1000, "payment");
  c.monthEnd();
  assertEqual(c.history.length, 2);
  assertEqual(c.balance, 0);
}

export function testCreditPaymentCannotExceedOwed(): void {
  const c = new CreditAccount("c", 50000, 2400);
  assertThrows(() => { c.deposit(1); }, "payment exceeds balance owed on c");
  c.withdraw(2500);
  assertThrows(() => { c.deposit(2501); }, "payment exceeds balance owed on c");
  c.deposit(2500, "payment");
  assertEqual(c.balance, 0);
  assertThrows(() => { c.deposit(-5); }, "invalid amount");
}

export function testTransferToCreditIsAtomic(): void {
  const b = bank();
  b.get("chk").deposit(10000);
  b.get("card").withdraw(3000, "groceries");
  assertThrows(() => { b.transfer("chk", "card", 3001); }, "payment exceeds balance owed on card");
  assertEqual(b.get("chk").balance, 10000);
  assertEqual(b.get("chk").history.length, 1);
  b.transfer("chk", "card", 3000);
  assertEqual(b.get("card").balance, 0);
  assertEqual(b.totalCents(), 7000);
}

export function testFreezeBlocksMoneyMovement(): void {
  const b = bank();
  b.get("chk").deposit(5000);
  b.freeze("chk");
  assert(b.get("chk").frozen);
  assertThrows(() => { b.get("chk").deposit(100); }, "account chk is frozen");
  assertThrows(() => { b.get("chk").withdraw(100); }, "account chk is frozen");
  assertThrows(() => { b.transfer("chk", "sav", 100); }, "account chk is frozen");
  assertEqual(b.get("chk").balance, 5000);
}

export function testTransferIntoFrozenAccountChangesNothing(): void {
  const b = bank();
  b.get("sav").deposit(10000);
  b.freeze("chk");
  assertThrows(() => { b.transfer("sav", "chk", 100); }, "account chk is frozen");
  assertEqual(b.get("sav").balance, 10000);
  // the failed transfer did not use up one of the three savings withdrawals
  for (let i = 0; i < 3; i++) b.get("sav").withdraw(100);
  assertThrows(() => { b.get("sav").withdraw(100); }, "withdrawal limit reached for sav");
}

export function testFrozenAccountsStillGetMonthEnd(): void {
  const b = bank();
  b.get("sav").deposit(100000);
  b.get("card").withdraw(120000 - 20000);
  b.get("chk").withdraw(1000);
  b.freeze("sav");
  b.freeze("card");
  b.freeze("chk");
  b.monthEnd();
  assertEqual(b.get("sav").balance, 100200);
  assertEqual(b.get("card").balance, -101666);
  assertEqual(b.get("chk").balance, -2500);
}

export function testUnfreeze(): void {
  const b = bank();
  b.freeze("chk");
  b.unfreeze("chk");
  assert(!b.get("chk").frozen);
  b.get("chk").deposit(100);
  assertEqual(b.get("chk").balance, 100);
  const s = new SavingsAccount("s", 0);
  s.freeze();
  assert(s.frozen);
  s.unfreeze();
  s.deposit(5);
  assertEqual(s.balance, 5);
}

export function testStatementShowsFrozen(): void {
  const b = bank();
  b.get("card").withdraw(1999, "book");
  b.freeze("card");
  assertEqual(b.get("card").statement(), "credit card (frozen)\nbook: -19.99\nbalance: -19.99");
  b.unfreeze("card");
  assertEqual(b.get("card").statement(), "credit card\nbook: -19.99\nbalance: -19.99");
}

export function testErrorOrder(): void {
  const b = bank();
  b.freeze("sav");
  assertThrows(() => { b.get("sav").deposit(0); }, "invalid amount");
  assertThrows(() => { b.transfer("sav", "sav", 100); }, "same account");
  assertThrows(() => { b.transfer("sav", "zzz", 100); }, "unknown account: zzz");
  assertThrows(() => { b.freeze("zzz"); }, "unknown account: zzz");
  // withdrawal errors come before deposit errors in a transfer
  b.get("chk").deposit(100);
  b.freeze("card");
  assertThrows(() => { b.transfer("chk", "card", 200); }, "account card is frozen");
  assertThrows(() => { b.transfer("chk", "card", 5101); }, "insufficient funds in chk");
}
