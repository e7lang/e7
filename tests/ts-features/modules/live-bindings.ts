// @feature: Module state and live bindings of imported variables
// @category: Modules
// @status: supported
import { count, increment } from "./_lib/counter.ts";
console.log(count);
increment();
increment();
console.log(count, increment());
