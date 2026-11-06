# Roadmap

## 0.1.0: the first release

E7 0.1.0 is a complete, tested language and VM for its domain: signing and verification, protocol parsing and
business rules, written in everyday TypeScript and delivered from a server. It contains:

- **The language**: a strict TypeScript subset ([SPEC.md](SPEC.md)) with classes, interfaces, generics, closures,
  destructuring, `try` / `catch` / `finally`, generators and iterators, `async` / `await` with promises, async
  generators and `for await`, regular expressions, literal types, recursive types, getters and setters, and
  fixed-width integer types. [TYPESCRIPT.md](TYPESCRIPT.md) lists 250 TypeScript features: 155 supported, 17
  partial, 78 unsupported, each with its reason and alternative.
- **The standard library** of JavaScript as Node.js 22 implements it for these types (strings, arrays, `Map` / `Set`,
  `JSON`, `Math`, `Date`, `BigInt`, typed arrays, `structuredClone`), plus `e7:crypto`, `e7:encoding`, `e7:json` and
  `e7:test`.
- **e7vm**: a register VM with a bytecode verifier, a sandbox (instruction budget, memory and stack limits, no
  ambient capabilities), module signatures, a debugger, and a C API with bindings for Swift, Kotlin / Java, C# and
  JavaScript (WebAssembly), plus module managers for staged rollout.
- **Tooling**: `e7c`, `e7`, a VS Code extension, a playground and an AI skill.
- **Verification**: differential testing against Node.js, fuzzing, GC-stress, allocation-failure and sanitizer runs,
  and eight independent security reviews ([PRODUCTION.md](PRODUCTION.md)).

Releases before 1.0 may still change the language, the C API or the bytecode format; each change is listed in the
[CHANGELOG](../CHANGELOG.md). A module's `requires` field (BYTECODE.md section 5) tells a VM whether it can run it.

## Next: a production pilot (beta)

E7 has not yet run in production. The next stage is a pilot in an app:

- **Shadow mode first**: E7 scripts compute next to the existing code, which still decides; results, errors and
  timings are compared and reported. Then a small rollout through the module managers (signed manifests, rollout
  percentages, kill switch).
- **Measured on real devices**: performance and memory on low-end target phones, GC pauses, module sizes, load
  times. The data goes into PRODUCTION.md.
- **Fixes the pilot finds** come first; the language grows only where pilot scripts need it.

A release with a pilot running in production is the beta stage.

## 1.0

1.0 needs:

- **Pilot data**: a production pilot that has run for a while, with its results published in PRODUCTION.md.
- **A third-party security audit** of the verifier, the loading path, the C API and the crypto wrappers (the reviews
  so far were done by independent AI review agents).
- **A format and API freeze**: the bytecode format and the C API are frozen, so every 1.x VM runs every 1.x module
  and existing hosts keep working.
- **Performance** on low-end target devices measured and acceptable.

## Waiting with a reason

These TypeScript features are not excluded by the rules of SPEC section 0.1, but each waits for a reason:

| Feature | Why it waits |
|---|---|
| `Intl` and locale formatting | Locale data differs between platforms; results would not be identical everywhere |
| `String.prototype.normalize` | Needs Unicode normalization tables (size on mobile) |
| `WeakMap` / `WeakSet` | Need ephemeron tables in the collector |
| Timers (`setTimeout`, `setInterval`), `queueMicrotask` | The host is the event loop (DESIGN.md D2); timers would make results depend on timing |
| Class expressions and mixins | Classes are not values in E7 |
| Namespaces, decorators, JSX | Little use in the target scripts; decorators need run-time metadata |
| `var`, non-ASCII identifiers | Rarely needed; `let` / `const` and ASCII names cover the target scripts |
| The `v` regular expression flag | Set notation (ES2024) is not in the engine yet |

Excluded for good by the rules of SPEC section 0.1 (about 60 rows of TYPESCRIPT.md): `any`, `unknown` as a declared
type, unions other than with `null` / `undefined` (including discriminated unions of object types), index signatures
and `Record`, `keyof` / `typeof` types, conditional, mapped and template literal types, intersection types, `Symbol`
values, `Proxy` / `Reflect`, `eval`.

## Later

- Incremental GC, if pilot data shows pauses that matter on low-end devices.
- More of the standard library as pilot scripts need it.
