export interface LogEntry { at: number; event: string; from: string; to: string }

const EVENTS = ["pay", "ship", "deliver", "cancel", "return", "refund"];

// "<state> <event>" -> next state
const TRANSITIONS = new Map<string, string>([
  ["pending pay", "paid"],
  ["pending cancel", "cancelled"],
  ["paid ship", "shipped"],
  ["paid cancel", "refunded"],
  ["shipped deliver", "delivered"],
  ["delivered return", "returned"],
  ["returned refund", "refunded"],
]);

const RETURN_WINDOW = 30 * 24 * 3600;

export class Order {
  readonly id: string;
  #state = "pending";
  #log: LogEntry[] = [];
  #lastAt: number;
  #deliveredAt = 0;

  constructor(id: string, createdAt: number) {
    this.id = id;
    this.#lastAt = createdAt;
  }

  get state(): string {
    return this.#state;
  }

  get history(): LogEntry[] {
    return this.#log.map((e) => ({ ...e }));
  }

  allowedEvents(): string[] {
    return EVENTS.filter((e) => TRANSITIONS.has(`${this.#state} ${e}`));
  }

  apply(event: string, at: number): void {
    if (!EVENTS.includes(event)) throw new Error(`unknown event: ${event}`);
    if (at < this.#lastAt) throw new Error("time went backwards");
    const next = TRANSITIONS.get(`${this.#state} ${event}`);
    if (next === undefined) throw new Error(`cannot ${event} an order that is ${this.#state}`);
    if (event === "return" && at - this.#deliveredAt > RETURN_WINDOW) throw new Error("return window closed");
    if (next === "delivered") this.#deliveredAt = at;
    this.#log.push({ at, event, from: this.#state, to: next });
    this.#state = next;
    this.#lastAt = at;
  }
}
