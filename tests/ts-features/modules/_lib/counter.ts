// helper module: module state and a live binding
export let count = 0;
export function increment(): number {
  count++;
  return count;
}
