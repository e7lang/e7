// a re-export is a dependency too: order_c (already initialized) and order_d
import { record } from "./order_log.ts";
export { dValue } from "./order_d.ts";
export * from "./order_c.ts";
record("b");
