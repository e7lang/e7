`solution.ts` is the account model of our budgeting app (`Account` with `CheckingAccount` and
`SavingsAccount`, and a `Bank` that holds them; amounts are integer cents). We are adding credit cards and
the ability to freeze an account. Please extend `solution.ts`:

```ts
export class CreditAccount extends Account {
  constructor(id: string, limitCents: number, aprBps: number)
}

// new on Account (so on every kind of account)
get frozen(): boolean
freeze(): void
unfreeze(): void

// new on Bank
freeze(id: string): void   // "unknown account: <id>" for an unknown id, like get()
unfreeze(id: string): void
```

**Credit accounts**

- `kind` is `"credit"`. The balance starts at 0 and goes negative as the card is used; a withdrawal may take
  it down to `-limitCents` (below that: the usual `insufficient funds in <id>`).
- A deposit is a payment: it may not take the balance above 0. Otherwise it throws
  `payment exceeds balance owed on <id>`.
- `monthEnd()`: when the balance is negative, book an entry `"interest"` of
  `-Math.round(-balance * aprBps / 12 / 10000)` cents (skip it when that rounds to 0). Interest is booked even
  if it takes the balance past the limit.

**Frozen accounts**

- `deposit` and `withdraw` on a frozen account throw `account <id> is frozen`, and so does a transfer from
  or to it. Month-end interest and fees are still booked on frozen accounts.
- `statement()` of a frozen account has ` (frozen)` at the end of its first line (`credit card (frozen)`).

**Errors and transfers**

- `deposit` checks, in this order: `invalid amount`, frozen, then the credit payment rule.
  `withdraw` checks: `invalid amount`, frozen, the savings withdrawal limit, then `insufficient funds`.
- `Bank.transfer` keeps its checks (`same account`, unknown accounts), then reports the withdrawal's error
  if there is one, then the deposit's. A transfer that fails must not change anything: no entries, no
  balance change, and it must not use up one of a savings account's three monthly withdrawals.

Everything else (checking, savings, statements, `totalCents`) keeps working exactly as it does now.
`solution.test.ts` has the current tests.
