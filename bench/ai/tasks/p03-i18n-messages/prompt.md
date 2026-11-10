`original.ts` is how our web app looks up UI strings: locale fallback, plural forms and `{param}` placeholders.
The mobile app downloads the same catalog and has to show exactly the same text, so please port it to E7 as
`solution.ts`. The app only calls `translate`, so keep that function with the same signature and the same result
for every input; everything else can change as E7 needs.

```ts
export function translate(catalogJson: string, locale: string, key: string, paramsJson: string): string
```

`solution.test.ts` has two examples.
