import { assert } from "e7:test";
const table: number[] = [1, 2, 3];
const bad = table[7] + 1;
export function testNeverRuns(): void { assert(bad > 0); }
