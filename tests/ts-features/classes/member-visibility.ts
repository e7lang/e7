// @feature: `public`, `protected`, `private` modifiers
// @category: Classes
// @status: supported
// @note: accepted and checked by tsc; E7 does not enforce them (`#private` is enforced)
class Account {
  public owner: string;
  protected balance = 0;
  private log: string[] = [];
  constructor(owner: string) { this.owner = owner; }
  deposit(n: number): void { this.balance += n; this.log.push("+" + n); }
  protected summary(): string { return `${this.owner}: ${this.balance}`; }
}
class Savings extends Account {
  report(): string { return this.summary() + " (savings)"; }
}
const s = new Savings("Ann");
s.deposit(5);
console.log(s.report(), s);
