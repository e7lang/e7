// E7 port of original.ts: normalizes and validates the "edit profile" form.
import { tryParse } from "e7:json";

interface Profile {
  displayName: string;
  email: string;
  phone?: string | undefined;
  age?: number | undefined;
  website?: string | undefined;
  tags: string[];
}

const KNOWN_FIELDS = ["displayName", "email", "phone", "age", "website", "tags"];
const ALIASES = new Map<string, string>([
  ["name", "displayName"],
  ["mail", "email"],
  ["tel", "phone"],
  ["url", "website"],
]);
const REQUIRED = ["displayName", "email"];
const MAX_TAGS = 5;

// \s in the original's regular expressions: the whitespace a form can contain
function isSpace(c: string): boolean {
  return c === " " || c === "\t" || c === "\n" || c === "\r" || c === "\f" || c === "\v" || c === " ";
}

function collapseSpaces(s: string): string {
  const words: string[] = [];
  let cur = "";
  for (const c of s) {
    if (isSpace(c)) {
      if (cur !== "") words.push(cur);
      cur = "";
    } else {
      cur += c;
    }
  }
  if (cur !== "") words.push(cur);
  return words.join(" ");
}

function hasSpace(s: string): boolean {
  for (const c of s) if (isSpace(c)) return true;
  return false;
}

function isDigit(c: string): boolean {
  return c >= "0" && c <= "9";
}

function normalizeName(raw: string): string {
  const name = collapseSpaces(raw);
  if (name.length < 2 || name.length > 40) throw new Error("must be 2-40 characters");
  return name;
}

// /^[^\s@]+@[^\s@]+\.[^\s@]+$/
function isEmail(s: string): boolean {
  if (hasSpace(s)) return false;
  const at = s.indexOf("@");
  if (at <= 0 || s.indexOf("@", at + 1) >= 0) return false;
  const domain = s.slice(at + 1);
  const dot = domain.indexOf(".", 1); // a dot with something before and after it
  return dot > 0 && dot < domain.length - 1;
}

function normalizeEmail(raw: string): string {
  const email = raw.trim().toLowerCase();
  if (!isEmail(email)) throw new Error("not a valid email address");
  return email;
}

function normalizePhone(raw: string): string {
  let digits = "";
  for (const c of raw) if (isDigit(c)) digits += c;
  if (digits.length === 11 && digits.startsWith("1")) digits = digits.slice(1);
  if (digits.length !== 10) throw new Error("must have 10 digits");
  return `(${digits.slice(0, 3)}) ${digits.slice(3, 6)}-${digits.slice(6)}`;
}

function normalizeAge(raw: string): number {
  const t = raw.trim();
  if (t === "" || ![...t].every((c) => isDigit(c))) throw new Error("must be a whole number");
  const age = parseInt(raw, 10);
  if (age < 13 || age > 120) throw new Error("must be between 13 and 120");
  return age;
}

function normalizeWebsite(raw: string): string {
  let url = raw.trim();
  const lower = url.toLowerCase();
  if (lower.startsWith("http://")) url = url.slice(7);
  else if (lower.startsWith("https://")) url = url.slice(8);
  while (url.endsWith("/")) url = url.slice(0, -1);
  const slash = url.indexOf("/");
  const host = slash < 0 ? url : url.slice(0, slash);
  if (hasSpace(url) || !host.includes(".")) throw new Error("not a valid URL");
  return "https://" + host.toLowerCase() + (slash < 0 ? "" : url.slice(slash));
}

function normalizeTags(raw: string): string[] {
  const tags = raw
    .replaceAll(";", ",")
    .split(",")
    .map((t) => collapseSpaces(t).toLowerCase())
    .filter((t) => t.length > 0);
  const unique = [...new Set(tags)];
  if (unique.length > MAX_TAGS) throw new Error(`at most ${MAX_TAGS} tags`);
  return unique;
}

interface NormalizeResult {
  profile: Profile | null;
  errors: string[]; // "field: message", in field order
  ignored: string[];
}

function normalizeProfile(form: Map<string, string>): NormalizeResult {
  const values = new Map<string, string>();
  const ignored: string[] = [];
  for (const [key, value] of form) {
    const field = ALIASES.get(key) ?? key;
    if (!KNOWN_FIELDS.includes(field)) {
      ignored.push(key);
      continue;
    }
    values.set(field, value);
  }
  // a field left blank counts as not given
  for (const [key, value] of [...values]) {
    if (value.trim() === "") values.delete(key);
  }

  const errors: string[] = [];
  // runs one normalizer; returns null when the field is missing or invalid
  function run<T>(field: string, normalize: (raw: string) => T): T | null {
    const raw = values.get(field);
    if (raw === undefined) {
      if (REQUIRED.includes(field)) errors.push(`${field}: is required`);
      return null;
    }
    try {
      return normalize(raw);
    } catch (e) {
      errors.push(`${field}: ${e instanceof Error ? e.message : String(e)}`);
      return null;
    }
  }
  const displayName = run("displayName", (s) => normalizeName(s));
  const email = run("email", (s) => normalizeEmail(s));
  const phone = run("phone", (s) => normalizePhone(s));
  const age = run("age", (s) => normalizeAge(s));
  const website = run("website", (s) => normalizeWebsite(s));
  const tags = run("tags", (s) => normalizeTags(s));

  if (errors.length > 0 || displayName === null || email === null) return { profile: null, errors, ignored };
  const profile: Profile = {
    displayName,
    email,
    phone: phone ?? undefined,
    age: age ?? undefined,
    website: website ?? undefined,
    tags: tags ?? [],
  };
  return { profile, errors, ignored };
}

interface Failure { ok: boolean; errors: string[] }
interface Success { ok: boolean; profile: Profile; ignored: string[] }

// Called by the app with the form as a JSON object of strings.
export function submitProfileForm(formJson: string): string {
  const form = tryParse(formJson) as Map<string, string> | null;
  if (form === null) {
    const f: Failure = { ok: false, errors: ["form: invalid json"] };
    return JSON.stringify(f);
  }
  const r = normalizeProfile(form);
  if (r.profile === null) {
    const f: Failure = { ok: false, errors: r.errors };
    return JSON.stringify(f);
  }
  const s: Success = { ok: true, profile: r.profile, ignored: r.ignored };
  return JSON.stringify(s);
}
