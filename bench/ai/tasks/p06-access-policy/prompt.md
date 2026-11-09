`original.ts` evaluates access requests against the policy document our admins edit (roles with inheritance,
allow/deny rules with wildcards and conditions). The desktop client needs to make exactly the same decisions
offline, so please port it to E7 as `solution.ts`. The client only calls `authorize`, so keep that function
with the same signature and the same output for every input; everything else can change as E7 needs.

```ts
export function authorize(policyJson: string, requestJson: string): string
```

`solution.test.ts` has two examples.
