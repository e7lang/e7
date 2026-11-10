// Evaluates an access request against the policy document edited in the admin console.
// Roles grant nothing by themselves: rules do. A matching "deny" rule always wins over "allow" rules.

type Effect = "allow" | "deny";

interface RoleDef {
  inherits?: string[];
}

type Condition =
  | { type: "owner" }
  | { type: "hours"; from: number; to: number } // UTC hours, [from, to), may wrap past midnight
  | { type: "ipPrefix"; prefixes: string[] };

interface Rule {
  effect: Effect;
  permission: string; // "docs:read", "docs:*", "*:delete", "*"
  roles?: string[]; // any of these roles; everyone when left out
  when?: Condition[]; // all must hold
}

interface Policy {
  roles: Record<string, RoleDef>;
  users: { [userId: string]: string[] };
  rules: Rule[];
}

interface AccessRequest {
  userId: string;
  permission: string;
  ownerId?: string;
  at: string; // ISO time
  ip: string;
}

interface Decision {
  decision: Effect;
  reason: string;
  roles: string[];
}

const patternCache: Record<string, RegExp> = {};

function permissionMatches(pattern: string, permission: string): boolean {
  if (!(pattern in patternCache)) {
    const source = pattern.replace(/[.+?^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*");
    patternCache[pattern] = new RegExp(`^${source}$`);
  }
  return patternCache[pattern].test(permission);
}

// a user's roles plus everything they inherit, sorted
function effectiveRoles(policy: Policy, userId: string): string[] {
  const seen = new Set<string>();
  const visit = (role: string) => {
    if (seen.has(role) || !(role in policy.roles)) return;
    seen.add(role);
    policy.roles[role].inherits?.forEach(visit);
  };
  (policy.users[userId] ?? []).forEach(visit);
  return [...seen].sort();
}

function conditionHolds(cond: Condition, req: AccessRequest, hour: number): boolean {
  switch (cond.type) {
    case "owner":
      return req.ownerId !== undefined && req.ownerId === req.userId;
    case "hours":
      return cond.from <= cond.to ? hour >= cond.from && hour < cond.to : hour >= cond.from || hour < cond.to;
    case "ipPrefix":
      return cond.prefixes.some((p) => req.ip.startsWith(p));
  }
}

export function evaluate(policy: Policy, req: AccessRequest): Decision {
  const time = new Date(req.at);
  if (isNaN(time.getTime())) throw new Error(`invalid time "${req.at}"`);
  if (!(req.userId in policy.users)) throw new Error(`unknown user "${req.userId}"`);
  const hour = time.getUTCHours();
  const roles = effectiveRoles(policy, req.userId);

  let allowedBy = -1;
  for (const [i, rule] of policy.rules.entries()) {
    if (!permissionMatches(rule.permission, req.permission)) continue;
    if (rule.roles && !rule.roles.some((r) => roles.includes(r))) continue;
    if (!(rule.when ?? []).every((c) => conditionHolds(c, req, hour))) continue;
    if (rule.effect === "deny") return { decision: "deny", reason: `denied by rule ${i + 1}`, roles };
    if (allowedBy < 0) allowedBy = i;
  }
  return allowedBy >= 0
    ? { decision: "allow", reason: `allowed by rule ${allowedBy + 1}`, roles }
    : { decision: "deny", reason: "no matching rule", roles };
}

// Called by the app with the policy and the request as JSON.
export function authorize(policyJson: string, requestJson: string): string {
  let policy: Policy, req: AccessRequest;
  try {
    policy = JSON.parse(policyJson);
    req = JSON.parse(requestJson);
  } catch {
    return JSON.stringify({ decision: "deny", reason: "invalid json", roles: [] });
  }
  try {
    return JSON.stringify(evaluate(policy, req));
  } catch (e) {
    return JSON.stringify({ decision: "deny", reason: (e as Error).message, roles: [] });
  }
}
