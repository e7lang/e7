// `export { x as default }` is a live binding; `export default x` (a let) copies the value
export let level = 1;
export function raise(): void {
  level *= 2;
}
export { level as default };
export const snapshot = level;
