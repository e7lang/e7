// @feature: Narrowing inside closures (`const` variables)
// @category: Narrowing
// @status: supported
function make(label: string | null): () => string {
  const l = label;
  if (l === null) return () => "none";
  return () => l.toUpperCase();
}
console.log(make("ok")(), make(null)());
