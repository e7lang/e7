// @feature: Parameter and return type annotations
// @category: Everyday Types
// @status: supported
function greet(person: string, times: number): string {
  return `Hello, ${person}`.repeat(times);
}
function log(msg: string): void {
  console.log("[log]", msg);
}
log(greet("Ada", 2));
