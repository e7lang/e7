// helper module for the modules/ samples
export const PI = 3.14159;
export function area(r: number): number {
  return PI * r * r;
}
export interface Shape { name: string; sides: number }
export type Sides = number;
export class Square implements Shape {
  name = "square";
  sides: Sides = 4;
  constructor(public size: number) {}
  area(): number { return this.size * this.size; }
}
export enum Kind { Round, Angular }
const secret = "hidden";
function describe(s: Shape): string { return `${s.name}/${s.sides}/${secret.length}`; }
export { describe, describe as explain };
