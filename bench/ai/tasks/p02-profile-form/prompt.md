`original.ts` normalizes and validates the "edit profile" form in our web app. The mobile app should accept and
reject exactly the same input and save exactly the same profile, so please port it to E7 as `solution.ts`. The
app only calls `submitProfileForm` (with the form as a JSON object of strings), so keep that function with the
same signature and the same output string for every input; everything else can change as E7 needs.

```ts
export function submitProfileForm(formJson: string): string
```

`solution.test.ts` has two examples.
