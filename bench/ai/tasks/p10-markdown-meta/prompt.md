`original.ts` builds the metadata of a blog post from its Markdown source (title, slug, excerpt, table of
contents, word count, reading time, links). The app's offline editor has to show exactly the same metadata as
the CMS, so please port it to E7 as `solution.ts`. The app only calls `articleMeta`, so keep that function with
the same signature and the same output for every input; everything else can change as E7 needs.

```ts
export function articleMeta(markdown: string): string
```

`solution.test.ts` has two examples.
