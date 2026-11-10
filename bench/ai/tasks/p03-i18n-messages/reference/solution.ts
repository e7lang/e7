// E7 port of original.ts: UI message lookup with locale fallback, plural forms and {param} placeholders.
import { tryParse } from "e7:json";

type Messages = Map<string, string>;
type Catalog = Map<string, Messages>;
type PluralForm = "zero" | "one" | "few" | "other";

// A parameter is a string or, when it looks like a number, a number (E7 has no string | number union).
interface ParamValue {
  text: string;
  num: number | null;
}
type Params = Map<string, ParamValue>;

interface NumberFormat {
  group: string;
  decimal: string;
}

const DEFAULT_LOCALE = "en";

const PLURAL_RULES = new Map<string, (n: number) => PluralForm>([
  ["en", (n) => (n === 1 ? "one" : "other")],
  ["de", (n) => (n === 1 ? "one" : "other")],
  ["fr", (n) => (n === 0 || n === 1 ? "one" : "other")],
  [
    "pl",
    (n) => {
      if (n === 1) return "one";
      const mod10 = n % 10, mod100 = n % 100;
      return mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14) ? "few" : "other";
    },
  ],
  ["ja", (n) => "other"],
]);

const NUMBER_FORMAT = new Map<string, NumberFormat>([
  ["en", { group: ",", decimal: "." }],
  ["de", { group: ".", decimal: "," }],
  ["fr", { group: " ", decimal: "," }],
  ["pl", { group: " ", decimal: "," }],
]);

const isLetter = (c: string) => (c >= "a" && c <= "z") || (c >= "A" && c <= "Z");
const isDigit = (c: string) => c >= "0" && c <= "9";
const isWordChar = (c: string) => isLetter(c) || isDigit(c) || c === "_";
const allLetters = (s: string) => [...s].every((c) => isLetter(c));
const allDigits = (s: string) => s.length > 0 && [...s].every((c) => isDigit(c));

// "pt_br" -> "pt-BR"; anything that is not a language tag falls back to the default locale
function normalizeLocale(tag: string): string {
  const s = tag.trim();
  const sep = Math.max(s.indexOf("-"), s.indexOf("_"));
  const lang = sep < 0 ? s : s.slice(0, sep);
  const region = sep < 0 ? "" : s.slice(sep + 1);
  if (lang.length < 2 || lang.length > 3 || !allLetters(lang)) return DEFAULT_LOCALE;
  if (sep < 0) return lang.toLowerCase();
  if (region.length !== 2 || !allLetters(region)) return DEFAULT_LOCALE;
  return `${lang.toLowerCase()}-${region.toUpperCase()}`;
}

function fallbackChain(locale: string): string[] {
  const chain = [locale];
  if (locale.includes("-")) chain.push(locale.split("-")[0]);
  chain.push(DEFAULT_LOCALE);
  return [...new Set(chain)];
}

function groupDigits(digits: string, sep: string): string {
  let out = "";
  for (let i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 === 0) out += sep;
    out += digits[i];
  }
  return out;
}

function formatNumber(n: number, lang: string): string {
  const fmt = NUMBER_FORMAT.get(lang) ?? NUMBER_FORMAT.get(DEFAULT_LOCALE)!;
  const parts = (Number.isInteger(n) ? String(n) : n.toFixed(2)).split(".");
  const int = parts[0];
  const sign = int.startsWith("-") ? "-" : "";
  const grouped = sign + groupDigits(int.slice(sign.length), fmt.group);
  return parts.length < 2 ? grouped : grouped + fmt.decimal + parts[1];
}

function formatValue(value: ParamValue, lang: string): string {
  return value.num !== null ? formatNumber(value.num, lang) : value.text;
}

// replaces {name} (name: letters, digits, _) by its parameter; unknown names stay as they are
function interpolate(template: string, params: Params, lang: string): string {
  let out = "";
  let i = 0;
  while (i < template.length) {
    const open = template.indexOf("{", i);
    if (open < 0) break;
    let j = open + 1;
    while (j < template.length && isWordChar(template[j])) j++;
    if (j > open + 1 && j < template.length && template[j] === "}") {
      const value = params.get(template.slice(open + 1, j));
      out += template.slice(i, open) + (value !== undefined ? formatValue(value, lang) : template.slice(open, j + 1));
      i = j + 1;
    } else {
      out += template.slice(i, open + 1);
      i = open + 1;
    }
  }
  return out + template.slice(i);
}

// /^-?\d+(\.\d+)?$/
function looksNumeric(v: string): boolean {
  const s = v.startsWith("-") ? v.slice(1) : v;
  const dot = s.indexOf(".");
  if (dot < 0) return allDigits(s);
  return allDigits(s.slice(0, dot)) && allDigits(s.slice(dot + 1));
}

// Parameters arrive as strings; numeric ones are formatted as numbers.
function toParams(raw: Map<string, string>): Params {
  const params: Params = new Map();
  for (const [name, v] of raw) params.set(name, { text: v, num: looksNumeric(v) ? Number(v) : null });
  return params;
}

function candidateKeys(key: string, lang: string, count: number | null): string[] {
  if (count === null) return [key];
  const rule = PLURAL_RULES.get(lang) ?? PLURAL_RULES.get(DEFAULT_LOCALE)!;
  const keys = [`${key}.${rule(count)}`, `${key}.other`, key];
  if (count === 0) keys.unshift(`${key}.zero`);
  return keys;
}

function lookup(catalog: Catalog, locale: string, key: string, params: Params): string {
  const count = params.get("count")?.num ?? null;
  for (const loc of fallbackChain(normalizeLocale(locale))) {
    const messages = catalog.get(loc);
    if (messages === undefined) continue;
    const lang = loc.split("-")[0];
    for (const candidate of candidateKeys(key, lang, count)) {
      const template = messages.get(candidate);
      if (template !== undefined) return interpolate(template, params, lang);
    }
  }
  return key; // a missing translation shows the key, so it gets noticed
}

// Called by the app: catalog and parameters as JSON, the result is the text to show.
export function translate(catalogJson: string, locale: string, key: string, paramsJson: string): string {
  const catalog = tryParse(catalogJson) as Catalog | null;
  const raw = paramsJson.trim() === "" ? new Map<string, string>() : (tryParse(paramsJson) as Map<string, string> | null);
  if (catalog === null || raw === null) return `[${key}: SyntaxError]`;
  return lookup(catalog, locale, key, toParams(raw));
}
