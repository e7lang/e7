import { assertEqual } from "e7:test";
import { resolveConfig } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testDevProfile(): void {
  assertEqual(resolveConfig('dev', '  '), '{"ok":true,"config":{"api":{"baseUrl":"http://localhost:8080","timeoutMs":10000,"retries":0,"headers":["accept: application/json"]},"logging":{"level":"debug","json":false,"redact":["password"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["debugPanel","search"]}}');
}

export function testStagingWithOverrides(): void {
  const overrides = '{"api":{"timeoutMs":2500,"headers":{"X-Client":" ios-7.2 ","Accept":"application/cbor"}},"cache":{"enabled":false,"maxEntries":0}}';
  assertEqual(resolveConfig('staging', overrides), '{"ok":true,"config":{"api":{"baseUrl":"https://staging.api.example.com","timeoutMs":2500,"retries":2,"headers":["accept: application/cbor","x-client: ios-7.2"]},"logging":{"level":"info","json":false,"redact":["password"]},"cache":{"enabled":false,"ttlSeconds":60,"maxEntries":0},"features":["search"]}}');
}

export function testNullKeepsValueAndRemovesHeader(): void {
  const overrides = '{"api":{"baseUrl":null,"retries":5,"headers":{"ACCEPT":null}},"logging":{"level":null,"redact":[]}}';
  assertEqual(resolveConfig('prod', overrides), '{"ok":true,"config":{"api":{"baseUrl":"https://api.example.com","timeoutMs":10000,"retries":5,"headers":[]},"logging":{"level":"warn","json":true,"redact":[]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["darkMode","search"]}}');
}

export function testUnknownSettingsIgnored(): void {
  const overrides = '{"api":{"basUrl":"https://typo.example.com","retries":1},"theme":{"color":"red"},"logging":{"colors":true}}';
  assertEqual(resolveConfig('dev', overrides), '{"ok":true,"config":{"api":{"baseUrl":"http://localhost:8080","timeoutMs":10000,"retries":1,"headers":["accept: application/json"]},"logging":{"level":"debug","json":false,"redact":["password"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["debugPanel","search"]}}');
}

export function testFeatureFlags(): void {
  const overrides = '{"features":{"search":false,"betaCheckout":true,"darkMode":true,"alpha":false}}';
  assertEqual(resolveConfig('prod', overrides), '{"ok":true,"config":{"api":{"baseUrl":"https://api.example.com","timeoutMs":10000,"retries":2,"headers":["accept: application/json"]},"logging":{"level":"warn","json":true,"redact":["password","token","ssn"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["betaCheckout","darkMode"]}}');
}

export function testArraysAreReplaced(): void {
  assertEqual(resolveConfig('dev', '{"logging":{"redact":["email"],"json":true}}'), '{"ok":true,"config":{"api":{"baseUrl":"http://localhost:8080","timeoutMs":10000,"retries":0,"headers":["accept: application/json"]},"logging":{"level":"debug","json":true,"redact":["email"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["debugPanel","search"]}}');
}

export function testEveryValidationError(): void {
  const overrides = '{"api":{"baseUrl":"ftp://files.example.com","timeoutMs":60001,"retries":1.5},"logging":{"level":"verbose"},"cache":{"maxEntries":-1,"ttlSeconds":-5}}';
  assertEqual(resolveConfig('staging', overrides), '{"ok":false,"errors":["api.baseUrl: invalid URL \\"ftp://files.example.com\\"","api.timeoutMs: must be 100-60000","api.retries: must be an integer 0-5","logging.level: unknown level \\"verbose\\"","cache.maxEntries: must be positive when the cache is enabled","cache.ttlSeconds: must not be negative"]}');
}

export function testBaseUrlWithPathAndPort(): void {
  const overrides = '{"api":{"baseUrl":"https://edge.example.com:8443/v2/","timeoutMs":60000,"retries":0}}';
  assertEqual(resolveConfig('prod', overrides), '{"ok":true,"config":{"api":{"baseUrl":"https://edge.example.com:8443/v2/","timeoutMs":60000,"retries":0,"headers":["accept: application/json"]},"logging":{"level":"warn","json":true,"redact":["password","token","ssn"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["darkMode","search"]}}');
}

export function testBaseUrlWithSpace(): void {
  assertEqual(resolveConfig('prod', '{"api":{"baseUrl":"https://example.com/a b"}}'), '{"ok":false,"errors":["api.baseUrl: invalid URL \\"https://example.com/a b\\""]}');
}

export function testBaseUrlWithoutHost(): void {
  assertEqual(resolveConfig('prod', '{"api":{"baseUrl":"https:///path"}}'), '{"ok":false,"errors":["api.baseUrl: invalid URL \\"https:///path\\""]}');
}

export function testCacheDisabledAllowsZeroEntries(): void {
  assertEqual(resolveConfig('staging', '{"cache":{"enabled":false,"maxEntries":0,"ttlSeconds":0}}'), '{"ok":true,"config":{"api":{"baseUrl":"https://staging.api.example.com","timeoutMs":10000,"retries":2,"headers":["accept: application/json"]},"logging":{"level":"info","json":false,"redact":["password"]},"cache":{"enabled":false,"ttlSeconds":0,"maxEntries":0},"features":["search"]}}');
}

export function testUnknownEnvironment(): void {
  assertEqual(resolveConfig('Prod', ''), '{"ok":false,"errors":["unknown environment \\"Prod\\""]}');
}

export function testInvalidJson(): void {
  assertEqual(resolveConfig('dev', '{api:'), '{"ok":false,"errors":["overrides: SyntaxError"]}');
}
