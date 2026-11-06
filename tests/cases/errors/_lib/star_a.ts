// helper: star-exported together with star_b.ts (both export `value`)
export const value = "a";
export const onlyA = 1;
export default function fromA(): number {
  return 1;
}
