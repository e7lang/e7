// Order lifecycle shown in the "My orders" screen. The app replays the events it gets from the
// backend to show the current state, and refuses transitions that are not allowed.

export enum OrderState {
  Created = "created",
  Paid = "paid",
  Shipped = "shipped",
  Delivered = "delivered",
  Cancelled = "cancelled",
}

export type OrderEvent = "pay" | "ship" | "deliver" | "cancel";

export interface Transition {
  event: OrderEvent;
  from: OrderState;
  to: OrderState;
  at: number; // Unix seconds
}

export function nextState(state: OrderState, event: OrderEvent): OrderState | null {
  switch (state) {
    case OrderState.Created:
      return event === "pay" ? OrderState.Paid : event === "cancel" ? OrderState.Cancelled : null;
    case OrderState.Paid:
      return event === "ship" ? OrderState.Shipped : event === "cancel" ? OrderState.Cancelled : null;
    case OrderState.Shipped:
      return event === "deliver" ? OrderState.Delivered : null;
    case OrderState.Delivered:
    case OrderState.Cancelled:
      return null;
  }
}

function parseEvent(text: string): OrderEvent | null {
  switch (text) {
    case "pay":
    case "ship":
    case "deliver":
    case "cancel":
      return text;
  }
  return null;
}

export class Order {
  state: OrderState = OrderState.Created;
  readonly history: Transition[] = [];

  constructor(readonly id: string, readonly totalCents: number) {}

  canApply(event: OrderEvent): boolean {
    return nextState(this.state, event) !== null;
  }

  apply(event: OrderEvent, at: number): void {
    const last = this.history.length > 0 ? this.history[this.history.length - 1].at : -Infinity;
    if (at < last) throw new Error(`event out of order: ${event}@${at}`);
    const to = nextState(this.state, event);
    if (to === null) throw new Error(`cannot ${event} order in state ${this.state}`);
    this.history.push({ event, from: this.state, to, at });
    this.state = to;
  }

  describe(): string {
    const steps = this.history.length;
    return `${this.id}: ${this.state} (${steps} ${steps === 1 ? "step" : "steps"})`;
  }
}

// Replays "pay@1700000000,ship@1700003600" and returns describe() of the result, or
// "error at <n>: <message>" for the first event (1-based) that fails.
export function replay(id: string, totalCents: number, events: string): string {
  const order = new Order(id, totalCents);
  const parts = events.trim() === "" ? [] : events.split(",");
  for (let i = 0; i < parts.length; i++) {
    const part = parts[i].trim();
    const sep = part.indexOf("@");
    const name = sep < 0 ? part : part.slice(0, sep);
    const time = sep < 0 ? "" : part.slice(sep + 1);
    const event = parseEvent(name);
    try {
      if (event === null) throw new Error(`unknown event ${name}`);
      if (time === "" || !Number.isInteger(Number(time))) throw new Error(`bad time for ${name}`);
      order.apply(event, Number(time));
    } catch (e) {
      return `error at ${i + 1}: ${e instanceof Error ? e.message : String(e)}`;
    }
  }
  return order.describe();
}
