`solution.ts` holds the role model of our back-office app (`RoleGraph`: roles inherit from other roles and grant
permissions like `orders:refund`, with `orders:*` and `*` wildcards). Security wants explicit deny rules, and
support wants to see *why* someone may or may not do something. Please extend `solution.ts`:

```ts
export interface RoleDef {
  name: string;
  inherits: string[];
  grants: string[];
  denies?: string[]; // new, optional; same pattern syntax as grants (wildcards included)
}

// new methods on RoleGraph
effectiveDenies(role: string): string[]
explain(userRoles: string[], permission: string): string
```

Rules:

- A deny always wins: if any role the user has, directly or through inheritance, has a deny pattern that
  matches the permission, `can` returns false, whatever grants exist (also a `*` grant on another role).
- `effectiveDenies(role)`: all deny patterns of the role and the roles it inherits from, without
  duplicates, sorted (like `effectivePermissions`, which keeps listing grants only).
- `explain(userRoles, permission)`: the user's roles are taken in resolution order: `resolve()` of each user
  role in the order given, concatenated, keeping only the first occurrence of each role. Then:
  - `"denied by <role> (<pattern>)"` for the first role in that order that has a matching deny; `<pattern>`
    is the first matching pattern in that role's `denies` list;
  - otherwise `"granted by <role> (<pattern>)"` for the first role with a matching grant (first matching pattern
    in its `grants`);
  - otherwise `"not granted"` (also for an empty `userRoles`).
- `can(userRoles, permission)` is true exactly when `explain` starts with `"granted"`.
- Unknown roles, duplicate roles and inheritance cycles throw the same errors as today, from every method.
- Role definitions without `denies` must behave exactly as they do now.

`solution.test.ts` has the current tests.
