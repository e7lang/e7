// Resolves the client configuration: built-in defaults, then the environment profile, then the
// overrides pushed from the admin console. Unknown settings are ignored; the result is validated.

type LogLevel = "debug" | "info" | "warn" | "error";

interface AppConfig {
  api: { baseUrl: string; timeoutMs: number; retries: number; headers: Record<string, string> };
  logging: { level: LogLevel; json: boolean; redact: string[] };
  cache: { enabled: boolean; ttlSeconds: number; maxEntries: number };
  features: Record<string, boolean>;
}

// In overrides, a header set to null removes it.
type Overrides = {
  api?: Partial<Omit<AppConfig["api"], "headers">> & { headers?: Record<string, string | null> };
  logging?: Partial<AppConfig["logging"]>;
  cache?: Partial<AppConfig["cache"]>;
  features?: Record<string, boolean>;
};

const LEVELS: readonly LogLevel[] = ["debug", "info", "warn", "error"];

const DEFAULTS: AppConfig = {
  api: { baseUrl: "https://api.example.com", timeoutMs: 10000, retries: 2, headers: { accept: "application/json" } },
  logging: { level: "info", json: false, redact: ["password"] },
  cache: { enabled: true, ttlSeconds: 300, maxEntries: 500 },
  features: { search: true, darkMode: false },
};

const PROFILES: { [env: string]: Overrides } = {
  dev: { api: { baseUrl: "http://localhost:8080", retries: 0 }, logging: { level: "debug" }, features: { debugPanel: true } },
  staging: { api: { baseUrl: "https://staging.api.example.com" }, cache: { ttlSeconds: 60 } },
  prod: { logging: { level: "warn", json: true, redact: ["password", "token", "ssn"] }, features: { darkMode: true } },
};

// copies the known settings of one section; missing and null values keep the base value
function mergeSection<T extends object>(base: T, patch: Partial<T> | undefined): T {
  const out = { ...base };
  if (!patch) return out;
  for (const key in base) {
    const value = patch[key as keyof T];
    if (value !== undefined && value !== null) out[key] = value as T[typeof key];
  }
  return out;
}

function mergeHeaders(base: Record<string, string>, patch: Record<string, string | null> = {}): Record<string, string> {
  const out = { ...base };
  for (const name in patch) {
    const key = name.toLowerCase();
    const value = patch[name];
    if (value === null) delete out[key];
    else out[key] = value.trim();
  }
  return out;
}

function applyOverrides(config: AppConfig, o: Overrides): AppConfig {
  const { headers, ...api } = o.api ?? {};
  return {
    api: { ...mergeSection(config.api, api), headers: mergeHeaders(config.api.headers, headers) },
    logging: mergeSection(config.logging, o.logging),
    cache: mergeSection(config.cache, o.cache),
    features: { ...config.features, ...o.features },
  };
}

function validate(c: AppConfig): string[] {
  const errors: string[] = [];
  if (!/^https?:\/\/[^\s/]+(\/\S*)?$/.test(c.api.baseUrl)) errors.push(`api.baseUrl: invalid URL "${c.api.baseUrl}"`);
  if (c.api.timeoutMs < 100 || c.api.timeoutMs > 60000) errors.push("api.timeoutMs: must be 100-60000");
  if (!Number.isInteger(c.api.retries) || c.api.retries < 0 || c.api.retries > 5) errors.push("api.retries: must be an integer 0-5");
  if (!LEVELS.includes(c.logging.level)) errors.push(`logging.level: unknown level "${c.logging.level}"`);
  if (c.cache.enabled && !(c.cache.maxEntries > 0)) errors.push("cache.maxEntries: must be positive when the cache is enabled");
  if (c.cache.ttlSeconds < 0) errors.push("cache.ttlSeconds: must not be negative");
  return errors;
}

// Called by the app: the environment name and the overrides JSON ("" for none).
export function resolveConfig(env: string, overridesJson: string): string {
  if (!(env in PROFILES)) return JSON.stringify({ ok: false, errors: [`unknown environment "${env}"`] });
  let overrides: Overrides = {};
  if (overridesJson.trim() !== "") {
    try {
      overrides = JSON.parse(overridesJson);
    } catch (e) {
      return JSON.stringify({ ok: false, errors: [`overrides: ${(e as Error).name}`] });
    }
  }
  const config = applyOverrides(applyOverrides(DEFAULTS, PROFILES[env]), overrides);
  const errors = validate(config);
  if (errors.length > 0) return JSON.stringify({ ok: false, errors });

  const headers = Object.entries(config.api.headers).map(([name, value]) => `${name}: ${value}`).sort();
  const features = Object.keys(config.features).filter((f) => config.features[f]).sort();
  return JSON.stringify({ ok: true, config: { ...config, api: { ...config.api, headers }, features } });
}
