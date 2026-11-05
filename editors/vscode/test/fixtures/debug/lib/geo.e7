export function scale(x: number): number {
  const k = 10;
  return x * k;
}

export class Point {
  constructor(public x: number, public y: number) {}
  norm(): number {
    const sq = this.x * this.x + this.y * this.y;
    return Math.sqrt(sq);
  }
}
