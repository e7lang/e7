// generic declarations exported from a module (instantiated by the importing file)
export function lastOf<T>(xs: T[]): T | undefined {
  return xs.length > 0 ? xs[xs.length - 1] : undefined;
}
export const pairOf = <A, B>(a: A, b: B): [A, B] => [a, b];
export class Queue<T> {
  private items: T[] = [];
  enqueue(x: T): void {
    this.items.push(x);
  }
  dequeue(): T | undefined {
    return this.items.shift();
  }
}
export interface Tagged<T> {
  tag: string;
  value: T;
}
export type Opt<T> = T | undefined;
