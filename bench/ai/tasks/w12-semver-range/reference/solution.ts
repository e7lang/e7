export interface SemVer { major: number; minor: number; patch: number; prerelease: string[] }

interface Comparator { op: string; v: SemVer }

function isDigits(s: string): boolean {
  return s.length > 0 && [...s].every((c) => c >= "0" && c <= "9");
}

function isNumericPart(s: string): boolean {
  return isDigits(s) && (s.length === 1 || s[0] !== "0");
}

function isBuildIdentifier(s: string): boolean {
  return s.length > 0 && [...s].every((c) => (c >= "0" && c <= "9") || (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c === "-");
}

function isPrereleaseIdentifier(s: string): boolean {
  return isDigits(s) ? isNumericPart(s) : isBuildIdentifier(s);
}

function tryParse(text: string): SemVer | null {
  let s = text.trim();
  if (s.startsWith("v")) s = s.slice(1);
  const plus = s.indexOf("+");
  if (plus >= 0) {
    if (!s.slice(plus + 1).split(".").every(isBuildIdentifier)) return null;
    s = s.slice(0, plus);
  }
  const dash = s.indexOf("-");
  const core = dash >= 0 ? s.slice(0, dash) : s;
  const prerelease = dash >= 0 ? s.slice(dash + 1).split(".") : [];
  const nums = core.split(".");
  if (nums.length !== 3 || !nums.every(isNumericPart) || !prerelease.every(isPrereleaseIdentifier)) return null;
  return { major: Number(nums[0]), minor: Number(nums[1]), patch: Number(nums[2]), prerelease };
}

export function parseVersion(text: string): SemVer {
  const v = tryParse(text);
  if (v === null) throw new Error(`invalid version: ${text}`);
  return v;
}

function cmpNum(a: number, b: number): number {
  return a < b ? -1 : a > b ? 1 : 0;
}

function cmpIdent(a: string, b: string): number {
  const an = isDigits(a), bn = isDigits(b);
  if (an && bn) return cmpNum(Number(a), Number(b));
  if (an) return -1;
  if (bn) return 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

function cmp(a: SemVer, b: SemVer): number {
  const core = cmpNum(a.major, b.major) || cmpNum(a.minor, b.minor) || cmpNum(a.patch, b.patch);
  if (core !== 0) return core;
  const pa = a.prerelease, pb = b.prerelease;
  if (pa.length === 0 || pb.length === 0) return cmpNum(pb.length, pa.length);
  for (let i = 0; i < Math.min(pa.length, pb.length); i++) {
    const c = cmpIdent(pa[i], pb[i]);
    if (c !== 0) return c;
  }
  return cmpNum(pa.length, pb.length);
}

export function compareVersions(a: string, b: string): number {
  return cmp(parseVersion(a), parseVersion(b));
}

function sameCore(a: SemVer, b: SemVer): boolean {
  return a.major === b.major && a.minor === b.minor && a.patch === b.patch;
}

function ver(major: number, minor: number, patch: number): SemVer {
  return { major, minor, patch, prerelease: [] };
}

function parseComparators(token: string, range: string): Comparator[] {
  if (token === "*") return [];
  for (const op of [">=", "<=", ">", "<", "=", "^", "~"]) {
    if (!token.startsWith(op)) continue;
    const v = tryParse(token.slice(op.length));
    if (v === null) break;
    if (op === "^") {
      const upper = v.major > 0 ? ver(v.major + 1, 0, 0) : v.minor > 0 ? ver(0, v.minor + 1, 0) : ver(0, 0, v.patch + 1);
      return [{ op: ">=", v }, { op: "<", v: upper }];
    }
    if (op === "~") return [{ op: ">=", v }, { op: "<", v: ver(v.major, v.minor + 1, 0) }];
    return [{ op, v }];
  }
  const v = tryParse(token);
  if (v === null) throw new Error(`invalid range: ${range}`);
  return [{ op: "=", v }];
}

function test(c: Comparator, v: SemVer): boolean {
  const r = cmp(v, c.v);
  switch (c.op) {
    case ">=": return r >= 0;
    case "<=": return r <= 0;
    case ">": return r > 0;
    case "<": return r < 0;
  }
  return r === 0;
}

function parseRange(range: string): Comparator[][] {
  return range.split("||").map((part) => {
    const tokens = part.trim().split(" ").filter((t) => t !== "");
    if (tokens.length === 0) throw new Error(`invalid range: ${range}`);
    return tokens.flatMap((t) => parseComparators(t, range));
  });
}

function satisfiesSet(v: SemVer, set: Comparator[]): boolean {
  if (!set.every((c) => test(c, v))) return false;
  if (v.prerelease.length === 0) return true;
  return set.some((c) => c.v.prerelease.length > 0 && sameCore(c.v, v));
}

export function satisfies(version: string, range: string): boolean {
  const sets = parseRange(range);
  const v = parseVersion(version);
  return sets.some((set) => satisfiesSet(v, set));
}

export function maxSatisfying(versions: string[], range: string): string | null {
  const sets = parseRange(range);
  let best: string | null = null;
  let bestV: SemVer | null = null;
  for (const text of versions) {
    const v = parseVersion(text);
    if (!sets.some((set) => satisfiesSet(v, set))) continue;
    if (bestV === null || cmp(v, bestV) > 0) {
      best = text;
      bestV = v;
    }
  }
  return best;
}
