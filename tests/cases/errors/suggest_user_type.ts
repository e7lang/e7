// @error: unknown type 'Persn'; did you mean 'Person'?
interface Person { name: string }
function f(p: Persn): string { return p.name; }
