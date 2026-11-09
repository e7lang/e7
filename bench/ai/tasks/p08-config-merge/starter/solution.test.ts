import { assertEqual } from "e7:test";
import { resolveConfig } from "./solution.ts";

export function testProdDefaults(): void {
  assertEqual(resolveConfig('prod', ''), '{"ok":true,"config":{"api":{"baseUrl":"https://api.example.com","timeoutMs":10000,"retries":2,"headers":["accept: application/json"]},"logging":{"level":"warn","json":true,"redact":["password","token","ssn"]},"cache":{"enabled":true,"ttlSeconds":300,"maxEntries":500},"features":["darkMode","search"]}}');
}

export function testInvalidTimeout(): void {
  assertEqual(resolveConfig('dev', '{"api":{"timeoutMs":50}}'), '{"ok":false,"errors":["api.timeoutMs: must be 100-60000"]}');
}
