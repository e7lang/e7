// star-exported by reexport_hub.ts together with reexport_b.ts
console.log("init reexport_a");
export const fromA = "A";
export const clash = "clash from A";
export function shared(): string {
  return "shared from A";
}
export default function aDefault(): string {
  return "default of A";
}
