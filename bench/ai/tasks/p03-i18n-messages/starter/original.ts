// UI message lookup: locale fallback (pt-BR -> pt -> en), plural forms and {param} placeholders.
// The catalog is downloaded as JSON: { "en": { "inbox.title": "Inbox", "inbox.count.one": "...", ... }, ... }

type Messages = { [key: string]: string };
type Catalog = Record<string, Messages>;
type ParamValue = string | number;
type Params = Record<string, ParamValue>;
type PluralForm = "zero" | "one" | "few" | "other";

const DEFAULT_LOCALE = "en";

const PLURAL_RULES: Record<string, (n: number) => PluralForm> = {
  en: (n) => (n === 1 ? "one" : "other"),
  de: (n) => (n === 1 ? "one" : "other"),
  fr: (n) => (n === 0 || n === 1 ? "one" : "other"),
  pl: (n) => {
    if (n === 1) return "one";
    const mod10 = n % 10, mod100 = n % 100;
    return mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14) ? "few" : "other";
  },
  ja: () => "other",
};

const NUMBER_FORMAT: { [lang: string]: { group: string; decimal: string } } = {
  en: { group: ",", decimal: "." },
  de: { group: ".", decimal: "," },
  fr: { group: " ", decimal: "," },
  pl: { group: " ", decimal: "," },
};

// "pt_br" -> "pt-BR"; anything that is not a language tag falls back to the default locale
function normalizeLocale(tag: string): string {
  const m = tag.trim().match(/^([a-z]{2,3})(?:[-_]([a-z]{2}))?$/i);
  if (!m) return DEFAULT_LOCALE;
  return m[2] ? `${m[1].toLowerCase()}-${m[2].toUpperCase()}` : m[1].toLowerCase();
}

function fallbackChain(locale: string): string[] {
  const chain = [locale];
  if (locale.includes("-")) chain.push(locale.split("-")[0]);
  chain.push(DEFAULT_LOCALE);
  return [...new Set(chain)];
}

function formatNumber(n: number, lang: string): string {
  const fmt = NUMBER_FORMAT[lang] ?? NUMBER_FORMAT[DEFAULT_LOCALE];
  const [int, frac] = (Number.isInteger(n) ? String(n) : n.toFixed(2)).split(".");
  const grouped = int.replace(/\B(?=(\d{3})+(?!\d))/g, fmt.group);
  return frac === undefined ? grouped : grouped + fmt.decimal + frac;
}

function formatValue(value: ParamValue, lang: string): string {
  return typeof value === "number" ? formatNumber(value, lang) : value;
}

function interpolate(template: string, params: Params, lang: string): string {
  return template.replace(/\{(\w+)\}/g, (placeholder, name: string) =>
    name in params ? formatValue(params[name], lang) : placeholder,
  );
}

// Parameters arrive as strings; numeric ones are formatted as numbers.
function toParams(raw: Record<string, string>): Params {
  const params: Params = {};
  for (const name in raw) {
    const v = raw[name];
    params[name] = /^-?\d+(\.\d+)?$/.test(v) ? Number(v) : v;
  }
  return params;
}

function candidateKeys(key: string, lang: string, count: number | undefined): string[] {
  if (count === undefined) return [key];
  const rule = PLURAL_RULES[lang] ?? PLURAL_RULES[DEFAULT_LOCALE];
  const keys = [`${key}.${rule(count)}`, `${key}.other`, key];
  if (count === 0) keys.unshift(`${key}.zero`);
  return keys;
}

export function t(catalog: Catalog, locale: string, key: string, params: Params = {}): string {
  const count = typeof params.count === "number" ? params.count : undefined;
  for (const loc of fallbackChain(normalizeLocale(locale))) {
    const messages = catalog[loc];
    if (!messages) continue;
    const lang = loc.split("-")[0];
    for (const candidate of candidateKeys(key, lang, count)) {
      if (candidate in messages) return interpolate(messages[candidate], params, lang);
    }
  }
  return key; // a missing translation shows the key, so it gets noticed
}

// Called by the app: catalog and parameters as JSON, the result is the text to show.
export function translate(catalogJson: string, locale: string, key: string, paramsJson: string): string {
  let catalog: Catalog;
  let raw: Record<string, string>;
  try {
    catalog = JSON.parse(catalogJson);
    raw = paramsJson.trim() === "" ? {} : JSON.parse(paramsJson);
  } catch (e) {
    return `[${key}: ${(e as Error).name}]`;
  }
  return t(catalog, locale, key, toParams(raw));
}
