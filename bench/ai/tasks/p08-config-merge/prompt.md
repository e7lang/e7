`original.ts` resolves our client configuration: built-in defaults, then the environment profile, then the
overrides pushed from the admin console, and validates the result. We are moving it into an E7 script so every
client resolves the configuration the same way, so please port it to E7 as `solution.ts`. The clients only call
`resolveConfig`, so keep that function with the same signature and the same output for every input; everything
else can change as E7 needs.

```ts
export function resolveConfig(env: string, overridesJson: string): string
```

`solution.test.ts` has two examples.
