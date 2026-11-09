// Order workflow of the shop backend: which events an order accepts in each status.
//
//   pending --pay--> paid --ship--> shipped --deliver--> delivered
//   pending, paid --cancel--> cancelled
//   cancelled, delivered --refund--> refunded   (only if the order was paid)

export type OrderStatus = "pending" | "paid" | "shipped" | "delivered" | "cancelled" | "refunded";
export type OrderEvent = "pay" | "ship" | "deliver" | "cancel" | "refund";

interface Order {
  id: string;
  status: OrderStatus;
  history: string[]; // "open", then every event that was applied
}

const NEW_ORDER: Order = { id: "", status: "pending", history: ["open"] };

export class OrderBook {
  private orders = new Map<string, Order>();

  open(id: string): string {
    if (this.orders.has(id)) return `duplicate order ${id}`;
    this.orders.set(id, { ...NEW_ORDER, id, history: [...NEW_ORDER.history] });
    return "ok";
  }

  status(id: string): string {
    return this.orders.get(id)?.status ?? "";
  }

  history(id: string): string[] {
    const order = this.orders.get(id);
    return order === undefined ? [] : [...order.history];
  }

  apply(id: string, event: OrderEvent): string {
    const order = this.orders.get(id);
    if (order === undefined) return `unknown order ${id}`;
    const result = this.transition(order, event);
    if (result !== "ok") return result;
    order.history.push(event);
    return "ok";
  }

  private transition(order: Order, event: OrderEvent): string {
    const from = order.status;
    let to: OrderStatus | null = null;
    switch (event) {
      case "pay":
        if (from === "pending") to = "paid";
        break;
      case "ship":
        if (from === "paid") to = "shipped";
        break;
      case "deliver":
        if (from === "shipped") to = "delivered";
        break;
      case "cancel":
        if (from === "pending" || from === "paid") to = "cancelled";
        else if (from === "shipped" || from === "delivered") return "cannot cancel a shipped order";
        break;
      case "refund":
        if (from === "cancelled" || from === "delivered") {
          if (!order.history.includes("pay")) return "nothing to refund";
          to = "refunded";
        }
        break;
    }
    if (to === null) return `cannot ${event} when ${from}`;
    order.status = to;
    return "ok";
  }
}
