// star-exported by reexport_hub.ts together with reexport_a.ts; also re-exports a's `shared` (the same
// binding through two `export *` is not ambiguous)
console.log("init reexport_b");
export { shared } from "./reexport_a.ts";
export const fromB = "B";
export const clash = "clash from B";
export let ticks = 0;
export function tick(): number {
  return ++ticks;
}
export type Label = { text: string };
