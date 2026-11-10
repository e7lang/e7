Notification and email texts are templates that the app fills in locally. Please write a small renderer as
`solution.ts` (an E7 script):

```ts
export function renderTemplate(template: string, vars: Map<string, string>): string
```

Syntax:

- `{{ name }}` is replaced by the variable's value, HTML-escaped: `&` → `&amp;`, `<` → `&lt;`, `>` → `&gt;`,
  `"` → `&quot;`, `'` → `&#39;`.
- `{{{ name }}}` inserts the value without escaping. It must be closed by `}}}`.
- Spaces around the name and around filters are ignored (`{{name}}` = `{{  name  }}`). A name is one or more
  ASCII letters, digits, `_` or `.` (`user.first_name` is just a name, looked up as is).
- Filters follow the name, separated by `|`, applied left to right: `upper`, `lower`, `trim`, and
  `default:<text>`. `default:` gives a fallback (the rest of that filter, trimmed) used when the variable is
  missing **or empty**; it can appear anywhere in the filter list and the other filters also apply to the
  fallback. Escaping (for `{{ }}`) happens after the filters.
- Everything outside tags is copied unchanged, including a stray `}}`.

Errors (throw `new Error(...)`); the first problem in the template, scanning left to right, wins, and for a
single tag the checks go in this order:

1. a `{{` (or `{{{`) without its closing `}}` (or `}}}`) → `unclosed tag at <i>`
2. an empty or invalid name → `invalid tag at <i>`
3. an unknown filter → `unknown filter "<filter>"` (the filter text, trimmed)
4. a missing variable without a default → `missing variable "<name>"`

`<i>` is the index of the tag's first `{` in the template.

`solution.test.ts` has a few examples.
