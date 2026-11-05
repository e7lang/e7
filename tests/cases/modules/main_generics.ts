// generics imported from another module, also through a namespace import
import { lastOf, pairOf, Queue } from "./_lib/generics.ts";
import type { Tagged, Opt } from "./_lib/generics.ts";
import * as G from "./_lib/generics.ts";
const q = new Queue<string>();
q.enqueue("a");
q.enqueue("b");
console.log(q.dequeue(), lastOf([1, 2, 3]), lastOf<string>([]), pairOf(1, "x"));
const t: Tagged<number[]> = { tag: "t", value: [1] };
const o: Opt<string> = undefined;
console.log(t, o, G.lastOf(["z"]), G.pairOf(true, [2]));
