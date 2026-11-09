import { assert, assertEqual, assertThrows } from "e7:test";
import { RoleGraph, matches } from "./solution.ts";
import type { RoleDef } from "./solution.ts";

function defs(): RoleDef[] {
  return [
    { name: "viewer", inherits: [], grants: ["orders:read", "customers:read"] },
    { name: "support", inherits: ["viewer"], grants: ["orders:refund", "tickets:*"] },
    { name: "admin", inherits: ["support"], grants: ["*"] },
  ];
}

export function testMatches(): void {
  assert(matches("orders:*", "orders:refund"));
  assert(!matches("orders:*", "ordersx:read"));
  assert(matches("*", "anything:else"));
}

export function testResolveAndEffective(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.resolve("admin"), ["admin", "support", "viewer"]);
  assertEqual(g.effectivePermissions("support"), ["customers:read", "orders:read", "orders:refund", "tickets:*"]);
}

export function testCan(): void {
  const g = new RoleGraph(defs());
  assert(g.can(["support"], "tickets:close"));
  assert(!g.can(["viewer"], "orders:refund"));
  assert(g.can(["viewer", "admin"], "billing:export"));
}

export function testErrors(): void {
  assertThrows(() => { new RoleGraph(defs()).resolve("owner"); }, "unknown role: owner");
  const cyclic: RoleDef[] = [
    { name: "a", inherits: ["b"], grants: [] },
    { name: "b", inherits: ["a"], grants: [] },
  ];
  assertThrows(() => { new RoleGraph(cyclic).resolve("a"); }, "role cycle: a -> b -> a");
}
