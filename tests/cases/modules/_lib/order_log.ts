// module state shared by the order_* modules: the order in which they were initialized
export const log: string[] = [];
export function record(name: string): void {
  log.push(name);
  console.log("init", name);
}
