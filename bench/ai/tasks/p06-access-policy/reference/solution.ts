// E7 port of original.ts: evaluates an access request against the policy document.
// Roles grant nothing by themselves: rules do. A matching "deny" rule always wins over "allow" rules.
import { tryParse } from "e7:json";

type Effect = "allow" | "deny";

interface RoleDef {
  inherits?: string[];
}

// One interface for the three condition kinds.
interface Condition {
  type: string; // "owner" | "hours" | "ipPrefix"
  from?: number; // hours: UTC hours, [from, to), may wrap past midnight
  to?: number;
  prefixes?: string[]; // ipPrefix
}

interface Rule {
  effect: string; // "allow" | "deny"
  permission: string; // "docs:read", "docs:*", "*:delete", "*"
  roles?: string[]; // any of these roles; everyone when left out
  when?: Condition[]; // all must hold
}

interface Policy {
  roles: Map<string, RoleDef>;
  users: Map<string, string[]>;
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

// glob match: "*" stands for any run of characters (also empty), everything else is literal
function permissionMatches(pattern: string, permission: string): boolean {
  let p = 0, s = 0, star = -1, mark = 0;
  while (s < permission.length) {
    if (p < pattern.length && pattern[p] !== "*" && pattern[p] === permission[s]) {
      p++;
      s++;
    } else if (p < pattern.length && pattern[p] === "*") {
      star = p++;
      mark = s;
    } else if (star >= 0) {
      p = star + 1;
      s = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.length && pattern[p] === "*") p++;
  return p === pattern.length;
}

// a user's roles plus everything they inherit, sorted
function effectiveRoles(policy: Policy, userId: string): string[] {
  const seen = new Set<string>();
  const visit = (role: string): void => {
    const def = policy.roles.get(role);
    if (seen.has(role) || def === undefined) return;
    seen.add(role);
    for (const r of def.inherits ?? []) visit(r);
  };
  for (const r of policy.users.get(userId) ?? []) visit(r);
  return [...seen].sort();
}

function conditionHolds(cond: Condition, req: AccessRequest, hour: number): boolean {
  switch (cond.type) {
    case "owner":
      return req.ownerId !== undefined && req.ownerId === req.userId;
    case "hours": {
      const from = cond.from ?? 0, to = cond.to ?? 0;
      return from <= to ? hour >= from && hour < to : hour >= from || hour < to;
    }
    case "ipPrefix":
      return (cond.prefixes ?? []).some((p) => req.ip.startsWith(p));
  }
  return false;
}

function evaluate(policy: Policy, req: AccessRequest): Decision {
  const time = new Date(req.at);
  if (isNaN(time.getTime())) throw new Error(`invalid time "${req.at}"`);
  if (!policy.users.has(req.userId)) throw new Error(`unknown user "${req.userId}"`);
  const hour = time.getUTCHours();
  const roles = effectiveRoles(policy, req.userId);

  let allowedBy = -1;
  for (let i = 0; i < policy.rules.length; i++) {
    const rule = policy.rules[i];
    if (!permissionMatches(rule.permission, req.permission)) continue;
    const ruleRoles = rule.roles;
    if (ruleRoles !== undefined && !ruleRoles.some((r) => roles.includes(r))) continue;
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
  const policy = tryParse(policyJson) as Policy | null;
  const req = tryParse(requestJson) as AccessRequest | null;
  if (policy === null || req === null) {
    const d: Decision = { decision: "deny", reason: "invalid json", roles: [] };
    return JSON.stringify(d);
  }
  try {
    return JSON.stringify(evaluate(policy, req));
  } catch (e) {
    const d: Decision = { decision: "deny", reason: e instanceof Error ? e.message : String(e), roles: [] };
    return JSON.stringify(d);
  }
}
