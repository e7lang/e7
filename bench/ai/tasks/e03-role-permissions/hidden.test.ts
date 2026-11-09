import { assert, assertEqual, assertThrows } from "e7:test";
import { RoleGraph, matches } from "./solution.ts";
import type { RoleDef } from "./solution.ts";

function defs(): RoleDef[] {
  return [
    { name: "viewer", inherits: [], grants: ["orders:read", "customers:read"] },
    { name: "support", inherits: ["viewer"], grants: ["orders:refund", "tickets:*"], denies: ["tickets:delete"] },
    { name: "trainee", inherits: ["support"], grants: [], denies: ["orders:*", "customers:read"] },
    { name: "billing", inherits: ["viewer"], grants: ["invoices:*", "orders:refund"] },
    { name: "admin", inherits: ["support", "billing"], grants: ["*"] },
    { name: "auditor", inherits: [], grants: ["*"], denies: ["*"] },
  ];
}

export function testOldDefinitionsWithoutDenies(): void {
  const g = new RoleGraph([
    { name: "viewer", inherits: [], grants: ["orders:read"] },
    { name: "editor", inherits: ["viewer"], grants: ["orders:*"] },
  ]);
  assert(g.can(["editor"], "orders:delete"));
  assert(!g.can(["viewer"], "orders:delete"));
  assertEqual(g.effectiveDenies("editor"), []);
  assertEqual(g.explain(["editor"], "orders:read"), "granted by editor (orders:*)");
}

export function testResolveRegression(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.resolve("admin"), ["admin", "support", "viewer", "billing"]);
  assertEqual(g.resolve("trainee"), ["trainee", "support", "viewer"]);
}

export function testEffectivePermissionsListsGrantsOnly(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.effectivePermissions("trainee"), ["customers:read", "orders:read", "orders:refund", "tickets:*"]);
  assertEqual(g.effectivePermissions("admin"), ["*", "customers:read", "invoices:*", "orders:read", "orders:refund", "tickets:*"]);
}

export function testEffectiveDenies(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.effectiveDenies("trainee"), ["customers:read", "orders:*", "tickets:delete"]);
  assertEqual(g.effectiveDenies("viewer"), []);
}

export function testDenyBeatsInheritedGrant(): void {
  const g = new RoleGraph(defs());
  assert(g.can(["support"], "tickets:close"));
  assert(!g.can(["support"], "tickets:delete"));
  assert(!g.can(["trainee"], "orders:read"));
  assert(g.can(["trainee"], "tickets:close"));
}

export function testDenyBeatsGrantFromAnotherUserRole(): void {
  const g = new RoleGraph(defs());
  // admin grants "*", but support (inherited by admin) denies tickets:delete
  assert(!g.can(["admin"], "tickets:delete"));
  assert(!g.can(["billing", "trainee"], "orders:refund"));
  assert(g.can(["billing", "support"], "invoices:send"));
}

export function testWildcardDeny(): void {
  const g = new RoleGraph(defs());
  assert(!g.can(["auditor"], "orders:read"));
  assertEqual(g.explain(["viewer", "auditor"], "orders:read"), "denied by auditor (*)");
}

export function testExplainGranted(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.explain(["support"], "orders:read"), "granted by viewer (orders:read)");
  assertEqual(g.explain(["support"], "tickets:close"), "granted by support (tickets:*)");
  assertEqual(g.explain(["admin"], "orders:refund"), "granted by admin (*)");
  // the first role in resolution order wins, and within a role the first matching pattern
  assertEqual(g.explain(["billing", "support"], "orders:refund"), "granted by billing (orders:refund)");
}

export function testExplainDenied(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.explain(["trainee"], "orders:refund"), "denied by trainee (orders:*)");
  assertEqual(g.explain(["admin"], "tickets:delete"), "denied by support (tickets:delete)");
  assertEqual(g.explain(["viewer", "trainee"], "customers:read"), "denied by trainee (customers:read)");
}

export function testExplainNotGranted(): void {
  const g = new RoleGraph(defs());
  assertEqual(g.explain(["viewer"], "orders:refund"), "not granted");
  assertEqual(g.explain([], "orders:read"), "not granted");
  assert(!g.can([], "orders:read"));
}

export function testErrorsStillThrown(): void {
  const g = new RoleGraph(defs());
  assertThrows(() => { g.explain(["viewer", "owner"], "orders:read"); }, "unknown role: owner");
  assertThrows(() => { g.can(["ghost"], "orders:read"); }, "unknown role: ghost");
  assertThrows(() => { new RoleGraph([...defs(), { name: "viewer", inherits: [], grants: [] }]); }, "duplicate role: viewer");
  const cyclic = new RoleGraph([
    { name: "a", inherits: ["b"], grants: ["x:y"] },
    { name: "b", inherits: ["c"], grants: [] },
    { name: "c", inherits: ["b"], grants: [], denies: ["x:y"] },
  ]);
  assertThrows(() => { cyclic.explain(["a"], "x:y"); }, "role cycle: a -> b -> c -> b");
}

export function testMatchesRegression(): void {
  assert(matches("orders:*", "orders:"));
  assert(!matches("orders:*", "orders"));
  assert(!matches("orders:read", "orders:readall"));
}
