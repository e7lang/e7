// E7 port of original.ts: resolves the client configuration from the built-in defaults, the environment
// profile and the overrides pushed from the admin console. Unknown settings are ignored; the result is validated.
import { tryParse } from "e7:json";

interface ApiConfig {
  baseUrl: string;
  timeoutMs: number;
  retries: number;
  headers: Map<string, string>;
}
interface LoggingConfig {
  level: string; // "debug" | "info" | "warn" | "error", checked by validate()
  json: boolean;
  redact: string[];
}
interface CacheConfig {
  enabled: boolean;
  ttlSeconds: number;
  maxEntries: number;
}
interface AppConfig {
  api: ApiConfig;
  logging: LoggingConfig;
  cache: CacheConfig;
  features: Map<string, boolean>;
}

// Overrides: every setting may be left out or null (null keeps the value; a null header removes it).
interface ApiOverrides {
  baseUrl?: string | null;
  timeoutMs?: number | null;
  retries?: number | null;
  headers?: Map<string, string | null> | null;
}
interface LoggingOverrides {
  level?: string | null;
  json?: boolean | null;
  redact?: string[] | null;
}
interface CacheOverrides {
  enabled?: boolean | null;
  ttlSeconds?: number | null;
  maxEntries?: number | null;
}
interface Overrides {
  api?: ApiOverrides | null;
  logging?: LoggingOverrides | null;
  cache?: CacheOverrides | null;
  features?: Map<string, boolean> | null;
}

const LEVELS = ["debug", "info", "warn", "error"];

const DEFAULTS: AppConfig = {
  api: { baseUrl: "https://api.example.com", timeoutMs: 10000, retries: 2, headers: new Map([["accept", "application/json"]]) },
  logging: { level: "info", json: false, redact: ["password"] },
  cache: { enabled: true, ttlSeconds: 300, maxEntries: 500 },
  features: new Map([["search", true], ["darkMode", false]]),
};

const PROFILES = new Map<string, Overrides>([
  ["dev", { api: { baseUrl: "http://localhost:8080", retries: 0 }, logging: { level: "debug" }, features: new Map([["debugPanel", true]]) }],
  ["staging", { api: { baseUrl: "https://staging.api.example.com" }, cache: { ttlSeconds: 60 } }],
  ["prod", { logging: { level: "warn", json: true, redact: ["password", "token", "ssn"] }, features: new Map([["darkMode", true]]) }],
]);

function mergeHeaders(base: Map<string, string>, patch: Map<string, string | null>): Map<string, string> {
  const out = new Map(base);
  for (const [name, value] of patch) {
    const key = name.toLowerCase();
    if (value === null) out.delete(key);
    else out.set(key, value.trim());
  }
  return out;
}

function applyOverrides(c: AppConfig, o: Overrides): AppConfig {
  const api = o.api ?? {};
  const logging = o.logging ?? {};
  const cache = o.cache ?? {};
  const features = new Map(c.features);
  for (const [name, on] of o.features ?? new Map<string, boolean>()) features.set(name, on);
  return {
    api: {
      baseUrl: api.baseUrl ?? c.api.baseUrl,
      timeoutMs: api.timeoutMs ?? c.api.timeoutMs,
      retries: api.retries ?? c.api.retries,
      headers: mergeHeaders(c.api.headers, api.headers ?? new Map<string, string | null>()),
    },
    logging: {
      level: logging.level ?? c.logging.level,
      json: logging.json ?? c.logging.json,
      redact: logging.redact ?? c.logging.redact,
    },
    cache: {
      enabled: cache.enabled ?? c.cache.enabled,
      ttlSeconds: cache.ttlSeconds ?? c.cache.ttlSeconds,
      maxEntries: cache.maxEntries ?? c.cache.maxEntries,
    },
    features,
  };
}

function hasSpace(s: string): boolean {
  for (const ch of s) if (ch.trim() === "") return true;
  return false;
}

// /^https?:\/\/[^\s/]+(\/\S*)?$/
function validUrl(url: string): boolean {
  let rest: string;
  if (url.startsWith("http://")) rest = url.slice(7);
  else if (url.startsWith("https://")) rest = url.slice(8);
  else return false;
  const slash = rest.indexOf("/");
  const host = slash < 0 ? rest : rest.slice(0, slash);
  return host.length > 0 && !hasSpace(rest);
}

function validate(c: AppConfig): string[] {
  const errors: string[] = [];
  if (!validUrl(c.api.baseUrl)) errors.push(`api.baseUrl: invalid URL "${c.api.baseUrl}"`);
  if (c.api.timeoutMs < 100 || c.api.timeoutMs > 60000) errors.push("api.timeoutMs: must be 100-60000");
  if (!Number.isInteger(c.api.retries) || c.api.retries < 0 || c.api.retries > 5) errors.push("api.retries: must be an integer 0-5");
  if (!LEVELS.includes(c.logging.level)) errors.push(`logging.level: unknown level "${c.logging.level}"`);
  if (c.cache.enabled && !(c.cache.maxEntries > 0)) errors.push("cache.maxEntries: must be positive when the cache is enabled");
  if (c.cache.ttlSeconds < 0) errors.push("cache.ttlSeconds: must not be negative");
  return errors;
}

interface Failure {
  ok: boolean;
  errors: string[];
}
interface ResolvedConfig {
  api: { baseUrl: string; timeoutMs: number; retries: number; headers: string[] };
  logging: LoggingConfig;
  cache: CacheConfig;
  features: string[];
}
interface Success {
  ok: boolean;
  config: ResolvedConfig;
}

function fail(errors: string[]): string {
  const f: Failure = { ok: false, errors };
  return JSON.stringify(f);
}

// Called by the app: the environment name and the overrides JSON ("" for none).
export function resolveConfig(env: string, overridesJson: string): string {
  const profile = PROFILES.get(env);
  if (profile === undefined) return fail([`unknown environment "${env}"`]);
  let overrides: Overrides = {};
  if (overridesJson.trim() !== "") {
    const parsed = tryParse(overridesJson) as Overrides | null;
    if (parsed === null) return fail(["overrides: SyntaxError"]);
    overrides = parsed;
  }
  const config = applyOverrides(applyOverrides(DEFAULTS, profile), overrides);
  const errors = validate(config);
  if (errors.length > 0) return fail(errors);

  const headers = [...config.api.headers].map(([name, value]) => `${name}: ${value}`).sort();
  const features = [...config.features].filter(([, on]) => on).map(([name]) => name).sort();
  const s: Success = {
    ok: true,
    config: {
      api: { baseUrl: config.api.baseUrl, timeoutMs: config.api.timeoutMs, retries: config.api.retries, headers },
      logging: config.logging,
      cache: config.cache,
      features,
    },
  };
  return JSON.stringify(s);
}
