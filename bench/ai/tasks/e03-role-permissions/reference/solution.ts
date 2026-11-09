// Role-based permissions for the back-office app. Roles inherit from other roles; a permission is
// "resource:action", and a grant may be a wildcard ("orders:*" or "*").

export interface RoleDef {
  name: string;
  inherits: string[];
  grants: string[];
  denies?: string[];
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

  // All denies of the role and the roles it inherits from, without duplicates, sorted.
  effectiveDenies(role: string): string[] {
    const all = new Set<string>();
    for (const name of this.resolve(role)) {
      for (const d of this.def(name).denies ?? []) all.add(d);
    }
    return [...all].sort();
  }

  // The roles of a user in resolution order, each once.
  private resolveAll(userRoles: string[]): string[] {
    const order: string[] = [];
    const seen = new Set<string>();
    for (const role of userRoles) {
      for (const name of this.resolve(role)) {
        if (seen.has(name)) continue;
        seen.add(name);
        order.push(name);
      }
    }
    return order;
  }

  explain(userRoles: string[], permission: string): string {
    const order = this.resolveAll(userRoles);
    for (const name of order) {
      const deny = (this.def(name).denies ?? []).find((d) => matches(d, permission));
      if (deny !== undefined) return `denied by ${name} (${deny})`;
    }
    for (const name of order) {
      const grant = this.def(name).grants.find((g) => matches(g, permission));
      if (grant !== undefined) return `granted by ${name} (${grant})`;
    }
    return "not granted";
  }

  can(userRoles: string[], permission: string): boolean {
    return this.explain(userRoles, permission).startsWith("granted");
  }
}
