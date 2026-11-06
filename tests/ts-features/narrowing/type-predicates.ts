// @feature: Type predicates (`x is T`)
// @category: Narrowing
// @status: unsupported
// @why: not implemented yet: user-defined type guards are not parsed
// @instead: `instanceof`, `=== null` checks, or a function that returns the narrowed value or `null`
// @error: type predicates ('x is T') are not supported
function isDefined(s: string | undefined): s is string {
  return s !== undefined;
}
const v: string | undefined = "a";
if (isDefined(v)) console.log(v.length);
