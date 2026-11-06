// helper module: a class as the default export
export default class Point {
  constructor(public x: number, public y: number) {}
  length(): number { return Math.hypot(this.x, this.y); }
}
