export interface QueryParam { key: string; value: string }

const HEX = "0123456789ABCDEF";

function isUnreserved(b: number): boolean {
  return (b >= 0x41 && b <= 0x5a) || (b >= 0x61 && b <= 0x7a) || (b >= 0x30 && b <= 0x39) ||
    b === 0x2d || b === 0x2e || b === 0x5f || b === 0x7e;
}

function encode(text: string): string {
  let out = "";
  for (const b of new TextEncoder().encode(text)) {
    if (isUnreserved(b)) out += String.fromCharCode(b);
    else if (b === 0x20) out += "+";
    else out += "%" + HEX[b >> 4] + HEX[b & 15];
  }
  return out;
}

function decode(text: string, piece: string): string {
  try {
    return decodeURIComponent(text.replaceAll("+", " "));
  } catch (e) {
    if (e instanceof URIError) throw new Error(`invalid encoding: ${piece}`);
    throw e;
  }
}

export function parseQuery(query: string): QueryParam[] {
  const q = query.startsWith("?") ? query.slice(1) : query;
  const out: QueryParam[] = [];
  for (const piece of q.split("&")) {
    if (piece === "") continue;
    const eq = piece.indexOf("=");
    const key = eq < 0 ? piece : piece.slice(0, eq);
    const value = eq < 0 ? "" : piece.slice(eq + 1);
    out.push({ key: decode(key, piece), value: decode(value, piece) });
  }
  return out;
}

export function buildQuery(params: QueryParam[]): string {
  return params.map((p) => `${encode(p.key)}=${encode(p.value)}`).join("&");
}

export function getParam(query: string, key: string): string | null {
  const found = parseQuery(query).find((p) => p.key === key);
  return found === undefined ? null : found.value;
}

export function setParam(url: string, key: string, value: string): string {
  const hash = url.indexOf("#");
  const fragment = hash < 0 ? "" : url.slice(hash);
  const beforeHash = hash < 0 ? url : url.slice(0, hash);
  const qmark = beforeHash.indexOf("?");
  const base = qmark < 0 ? beforeHash : beforeHash.slice(0, qmark);
  const params = qmark < 0 ? [] : parseQuery(beforeHash.slice(qmark + 1));
  const out: QueryParam[] = [];
  let replaced = false;
  for (const p of params) {
    if (p.key !== key) out.push(p);
    else if (!replaced) {
      out.push({ key, value });
      replaced = true;
    }
  }
  if (!replaced) out.push({ key, value });
  return `${base}?${buildQuery(out)}${fragment}`;
}
