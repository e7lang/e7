function escapeHtml(s: string): string {
  return s.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;").replaceAll("'", "&#39;");
}

function isNameChar(c: string): boolean {
  return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || (c >= "0" && c <= "9") || c === "_" || c === ".";
}

function isValidName(name: string): boolean {
  return name.length > 0 && [...name].every(isNameChar);
}

function evalTag(body: string, at: number, vars: Map<string, string>): string {
  const parts = body.split("|").map((p) => p.trim());
  const name = parts[0];
  if (!isValidName(name)) throw new Error(`invalid tag at ${at}`);
  let fallback: string | null = null;
  const filters: string[] = [];
  for (const f of parts.slice(1)) {
    if (f.startsWith("default:")) fallback = f.slice(8).trim();
    else if (f === "upper" || f === "lower" || f === "trim") filters.push(f);
    else throw new Error(`unknown filter "${f}"`);
  }
  const found = vars.get(name);
  let value: string;
  if (found !== undefined && (found !== "" || fallback === null)) value = found;
  else if (fallback !== null) value = fallback;
  else throw new Error(`missing variable "${name}"`);
  for (const f of filters) {
    if (f === "upper") value = value.toUpperCase();
    else if (f === "lower") value = value.toLowerCase();
    else value = value.trim();
  }
  return value;
}

export function renderTemplate(template: string, vars: Map<string, string>): string {
  let out = "";
  let pos = 0;
  while (true) {
    const open = template.indexOf("{{", pos);
    if (open < 0) break;
    out += template.slice(pos, open);
    const raw = template.startsWith("{{{", open);
    const closer = raw ? "}}}" : "}}";
    const bodyStart = open + (raw ? 3 : 2);
    const close = template.indexOf(closer, bodyStart);
    if (close < 0) throw new Error(`unclosed tag at ${open}`);
    const value = evalTag(template.slice(bodyStart, close), open, vars);
    out += raw ? value : escapeHtml(value);
    pos = close + closer.length;
  }
  return out + template.slice(pos);
}
