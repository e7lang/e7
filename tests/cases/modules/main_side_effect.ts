// side-effect imports (`import "./x"`) and re-exports run their modules in JavaScript's order: depth first,
// in source order, each module once
console.log("main is initialized last");
import "./_lib/order_a.ts";
import { log } from "./_lib/order_log.ts";
import { cValue, dValue } from "./_lib/order_b.ts";
import "./_lib/order_a.ts";
import "./_lib/order_d.ts";
console.log(log.join(" "), cValue, dValue);
