// The account model behind the budgeting app's "envelopes". Amounts are integer cents.

export interface Entry {
  memo: string;
  cents: number; // positive for money in, negative for money out
  balanceAfter: number;
}

export function formatCents(cents: number): string {
  const sign = cents < 0 ? "-" : "";
  const abs = Math.abs(cents);
  return `${sign}${Math.floor(abs / 100)}.${String(abs % 100).padStart(2, "0")}`;
}

function checkAmount(cents: number): void {
  if (!Number.isInteger(cents) || cents <= 0) throw new Error("invalid amount");
}

export abstract class Account {
  private entries: Entry[] = [];
  private balanceCents = 0;

  constructor(readonly id: string) {}

  abstract get kind(): string;
  // The lowest balance a withdrawal may leave.
  protected abstract floor(): number;
  // Interest and fees, called by the bank at the end of every month.
  abstract monthEnd(): void;
  // Extra checks of a subclass before a withdrawal (throws to refuse it).
  protected checkWithdrawal(cents: number): void {}

  get balance(): number {
    return this.balanceCents;
  }

  get history(): Entry[] {
    return [...this.entries];
  }

  // Books an entry without any checks (interest, fees).
  protected post(memo: string, cents: number): void {
    this.balanceCents += cents;
    this.entries.push({ memo, cents, balanceAfter: this.balanceCents });
  }

  deposit(cents: number, memo: string = "deposit"): void {
    checkAmount(cents);
    this.post(memo, cents);
  }

  withdraw(cents: number, memo: string = "withdrawal"): void {
    checkAmount(cents);
    this.checkWithdrawal(cents);
    if (this.balanceCents - cents < this.floor()) throw new Error(`insufficient funds in ${this.id}`);
    this.post(memo, -cents);
  }

  statement(): string {
    const lines = [`${this.kind} ${this.id}`];
    for (const e of this.entries) lines.push(`${e.memo}: ${e.cents > 0 ? "+" : ""}${formatCents(e.cents)}`);
    lines.push(`balance: ${formatCents(this.balanceCents)}`);
    return lines.join("\n");
  }
}

export class CheckingAccount extends Account {
  constructor(id: string, private overdraftCents: number = 0) {
    super(id);
  }
  get kind(): string {
    return "checking";
  }
  protected floor(): number {
    return -this.overdraftCents;
  }
  monthEnd(): void {
    if (this.balance < 0) this.post("overdraft fee", -1500);
  }
}

export class SavingsAccount extends Account {
  private withdrawalsThisMonth = 0;
  constructor(id: string, private rateBps: number) {
    super(id);
  }
  get kind(): string {
    return "savings";
  }
  protected floor(): number {
    return 0;
  }
  protected checkWithdrawal(cents: number): void {
    if (this.withdrawalsThisMonth >= 3) throw new Error(`withdrawal limit reached for ${this.id}`);
  }
  withdraw(cents: number, memo: string = "withdrawal"): void {
    super.withdraw(cents, memo);
    this.withdrawalsThisMonth++;
  }
  monthEnd(): void {
    const interest = Math.round((this.balance * this.rateBps) / 12 / 10000);
    if (interest > 0) this.post("interest", interest);
    this.withdrawalsThisMonth = 0;
  }
}

export class Bank {
  private accounts = new Map<string, Account>();

  add(account: Account): void {
    if (this.accounts.has(account.id)) throw new Error(`duplicate account: ${account.id}`);
    this.accounts.set(account.id, account);
  }

  get(id: string): Account {
    const account = this.accounts.get(id);
    if (account === undefined) throw new Error(`unknown account: ${id}`);
    return account;
  }

  transfer(fromId: string, toId: string, cents: number): void {
    if (fromId === toId) throw new Error("same account");
    const from = this.get(fromId);
    const to = this.get(toId);
    from.withdraw(cents, `transfer to ${toId}`);
    to.deposit(cents, `transfer from ${fromId}`);
  }

  monthEnd(): void {
    for (const account of this.accounts.values()) account.monthEnd();
  }

  totalCents(): number {
    let total = 0;
    for (const account of this.accounts.values()) total += account.balance;
    return total;
  }
}
