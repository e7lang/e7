// default export: a class; named exports beside it (classes, a generic class, an enum, types, module state)
console.log("init geometry");
export default class Vec {
  static created = 0;
  constructor(public x: number, public y: number) {
    Vec.created++;
  }
  static zero(): Vec {
    return new Vec(0, 0);
  }
  length(): number {
    return Math.hypot(this.x, this.y);
  }
  plus(o: Vec): Vec {
    return new Vec(this.x + o.x, this.y + o.y);
  }
}
export class Shape {
  constructor(public name: string) {}
  area(): number {
    return 0;
  }
}
export class Box<T> {
  constructor(public value: T) {}
  map<U>(f: (v: T) => U): Box<U> {
    return new Box<U>(f(this.value));
  }
}
export enum Unit { Mm = 1, Cm = 10, M = 1000 }
export enum Dir { Up = "up", Down = "down" }
export interface Sized { size: number }
export type Pair<T> = [T, T];
export let moves = 0;
export function move(): number {
  return ++moves;
}
