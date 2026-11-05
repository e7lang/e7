// @requires-vm: 0.2.5
// #private fields and methods: only accessible inside the class body; not own enumerable
// properties (Object.keys, JSON.stringify, console.log and object spread do not show them).

class Account {
  #balance = 0;
  #history: string[] = [];
  static #count = 0;
  readonly id: number;
  owner: string;
  constructor(owner: string, initial: number) {
    this.owner = owner;
    this.id = ++Account.#count;
    this.#deposit(initial, "open");
  }
  #deposit(amount: number, why: string): void {
    if (amount <= 0) throw new RangeError("amount must be positive");
    this.#balance += amount;
    this.#history.push(`${why}:${amount}`);
  }
  deposit(amount: number): number {
    this.#deposit(amount, "deposit");
    return this.#balance;
  }
  withdraw(amount: number): number {
    if (amount > this.#balance) throw new Error("insufficient funds");
    this.#balance -= amount;
    this.#history.push(`withdraw:${amount}`);
    return this.#balance;
  }
  get balance(): number {
    return this.#balance;
  }
  // private members of another instance of the same class
  transferTo(other: Account, amount: number): void {
    this.withdraw(amount);
    other.#deposit(amount, "transfer from " + this.owner);
  }
  history(): string {
    return this.#history.join(" ");
  }
  static created(): number {
    return Account.#count;
  }
  static #secret(): string {
    return "s" + Account.#count;
  }
  static reveal(): string {
    return Account.#secret();
  }
}

const a = new Account("ann", 100);
const b = new Account("bob", 5);
console.log(a.deposit(50), a.withdraw(30), a.balance);
a.transferTo(b, 20);
console.log(a.balance, b.balance, a.history(), "|", b.history());
try {
  a.withdraw(1000);
} catch (e) {
  console.log(String(e), a.balance);
}
try {
  a.deposit(-1);
} catch (e) {
  console.log(e instanceof RangeError, a.balance);
}
console.log(Account.created(), Account.reveal(), a.id, b.id);

// private fields are not visible to reflection
console.log(a);
console.log(JSON.stringify(a), Object.keys(a));
console.log({ ...a });
console.log([a, b].map((x) => x.owner));

// a class whose only fields are private
class Token {
  #value: string;
  constructor(v: string) {
    this.#value = v;
  }
  matches(other: Token): boolean {
    return this.#value === other.#value;
  }
}
const t1 = new Token("abc");
console.log(t1, JSON.stringify(t1), Object.keys(t1), t1.matches(new Token("abc")), t1.matches(new Token("x")));

// subclasses inherit the private state (accessed through the base class's methods)
class Savings extends Account {
  rate = 0.1;
  addInterest(): number {
    return this.deposit(this.balance * this.rate);
  }
}
const s = new Savings("sue", 200);
console.log(s.addInterest(), s, JSON.stringify(s));

// private fields in arrow functions and closures inside methods
class Clock {
  #ticks = 0;
  #listeners: ((t: number) => void)[] = [];
  on(f: (t: number) => void): void {
    this.#listeners.push(f);
  }
  tick(): void {
    this.#ticks++;
    this.#listeners.forEach((l) => l(this.#ticks));
  }
  #fmt = (n: number): string => `t${n}/${this.#ticks}`;
  format(n: number): string {
    return this.#fmt(n);
  }
}
const clock = new Clock();
const seen: number[] = [];
clock.on((t) => seen.push(t));
clock.tick();
clock.tick();
console.log(seen, clock.format(7), clock);

// optional and nullable private fields, compound assignment, ++
class Node2 {
  #next: Node2 | null = null;
  #label?: string;
  #n = 0;
  constructor(public v: number) {}
  link(n: Node2): Node2 {
    this.#next = n;
    this.#n += 2;
    this.#n++;
    return this;
  }
  label(): string {
    this.#label ??= "L" + this.v;
    return this.#label + this.#n + (this.#next ? "->" + this.#next.v : "");
  }
}
console.log(new Node2(1).link(new Node2(2)).label(), new Node2(3).label());

// records (object literals) may use '#' in quoted keys: those are ordinary properties
const rec = { "#tag": 1, plain: 2 };
console.log(rec, JSON.stringify(rec), Object.keys(rec));
