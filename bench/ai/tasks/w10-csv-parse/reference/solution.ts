function isLineEnd(text: string, i: number): boolean {
  return text[i] === "\n" || (text[i] === "\r" && i + 1 < text.length && text[i + 1] === "\n");
}

export function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  if (text === "") return rows;
  let row: string[] = [];
  let line = 1;
  let i = 0;
  while (true) {
    let field = "";
    if (i < text.length && text[i] === '"') {
      const startLine = line;
      i++;
      while (true) {
        if (i >= text.length) throw new Error(`unterminated quote starting on line ${startLine}`);
        const c = text[i];
        if (c === '"') {
          if (i + 1 < text.length && text[i + 1] === '"') {
            field += '"';
            i += 2;
            continue;
          }
          i++;
          break;
        }
        if (c === "\n") line++;
        field += c;
        i++;
      }
      if (i < text.length && text[i] !== "," && !isLineEnd(text, i)) {
        throw new Error(`unexpected character after quote on line ${line}`);
      }
    } else {
      while (i < text.length && text[i] !== "," && !isLineEnd(text, i)) field += text[i++];
    }
    row.push(field);
    if (i >= text.length) {
      rows.push(row);
      return rows;
    }
    if (text[i] === ",") {
      i++;
      continue;
    }
    i += text[i] === "\r" ? 2 : 1;
    line++;
    rows.push(row);
    row = [];
    if (i >= text.length) return rows;
  }
}

function quote(field: string): string {
  const needs = field.includes(",") || field.includes('"') || field.includes("\n") || field.includes("\r") ||
    field.startsWith(" ") || field.endsWith(" ");
  return needs ? `"${field.replaceAll('"', '""')}"` : field;
}

export function toCsv(rows: string[][]): string {
  return rows.map((r) => r.map(quote).join(",")).join("\r\n");
}

export function parseCsvObjects(text: string): Map<string, string>[] {
  const rows = parseCsv(text);
  if (rows.length === 0) return [];
  const header = rows[0];
  const out: Map<string, string>[] = [];
  for (let r = 1; r < rows.length; r++) {
    const row = rows[r];
    if (row.length !== header.length) {
      throw new Error(`record ${r + 1} has ${row.length} fields, expected ${header.length}`);
    }
    const m = new Map<string, string>();
    header.forEach((name, k) => m.set(name, row[k]));
    out.push(m);
  }
  return out;
}
