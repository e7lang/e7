// imported only with `import type` (directly, or through `export type ... from`): never run
console.log("typeonly_types must not run");
export interface Point { x: number; y: number }
export class Shape { constructor(public name: string) {} }
export enum Kind { Round, Square }
