import { assertEqual } from "e7:test";
import { parseCsv, toCsv, parseCsvObjects } from "./solution.ts";

export function testParse(): void {
  assertEqual(parseCsv('name,note\r\n"Smith, Jo","said ""hi"""\r\n'), [
    ["name", "note"],
    ["Smith, Jo", 'said "hi"'],
  ]);
}

export function testWrite(): void {
  assertEqual(toCsv([["a", "b,c"], ['5" disk', " x"]]), 'a,"b,c"\r\n"5"" disk"," x"');
}

export function testObjects(): void {
  const rows = parseCsvObjects("sku,price\npen,1.50\n");
  assertEqual(rows, [new Map<string, string>([["sku", "pen"], ["price", "1.50"]])]);
}
