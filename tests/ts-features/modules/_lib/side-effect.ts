// helper module: a module imported for its side effect
import { increment } from "./counter.ts";
increment();
console.log("side-effect module initialized");
