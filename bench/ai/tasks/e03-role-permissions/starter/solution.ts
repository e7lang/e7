// Role-based permissions for the back-office app. Roles inherit from other roles; a permission is
// "resource:action", and a grant may be a wildcard ("orders:*" or "*").

export interface RoleDef {
  name: string;
  inherits: string[];
  grants: string[];
}

// "*" matches everything, "orders:*" matches every "orders:..." permission, anything else must be equal.
export function matches(pattern: string, permission: string): boolean {
  if (pattern === "*") return true;
  if (pattern.endsWith(":*")) return permission.startsWith(pattern.slice(0, pattern.length - 1));
  return pattern === permission;
}

export class RoleGraph {
  private roles = new Map<string, RoleDef>();

  constructor(defs: RoleDef[]) {
    for (const def of defs) {
      if (this.roles.has(def.name)) throw new Error(`duplicate role: ${def.name}`);
      this.roles.set(def.name, def);
    }
  }

  private def(name: string): RoleDef {
    const def = this.roles.get(name);
    if (def === undefined) throw new Error(`unknown role: ${name}`);
    return def;
  }

  // The role itself, then the roles it inherits from, depth-first in the order they are listed.
  // Each role appears once (at its first position).
  resolve(role: string): string[] {
    const order: string[] = [];
    const seen = new Set<string>();
    const path: string[] = [];
    const visit = (name: string): void => {
      if (path.includes(name)) throw new Error(`role cycle: ${[...path, name].join(" -> ")}`);
      const def = this.def(name);
      if (seen.has(name)) return;
      seen.add(name);
      order.push(name);
      path.push(name);
      for (const parent of def.inherits) visit(parent);
      path.pop();
    };
    visit(role);
    return order;
  }

  // All grants of the role and the roles it inherits from, without duplicates, sorted.
  effectivePermissions(role: string): string[] {
    const all = new Set<string>();
    for (const name of this.resolve(role)) {
      for (const g of this.def(name).grants) all.add(g);
    }
    return [...all].sort();
  }

  can(userRoles: string[], permission: string): boolean {
    for (const role of userRoles) {
      for (const name of this.resolve(role)) {
        if (this.def(name).grants.some((g) => matches(g, permission))) return true;
      }
    }
    return false;
  }
}
