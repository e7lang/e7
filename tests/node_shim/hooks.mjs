const shims = {
  "e7:crypto": new URL("./e7-crypto.mjs", import.meta.url).href,
  "e7:encoding": new URL("./e7-encoding.mjs", import.meta.url).href,
  "e7:test": new URL("./e7-test.mjs", import.meta.url).href,
};
export async function resolve(specifier, context, next) {
  if (shims[specifier]) return { url: shims[specifier], shortCircuit: true };
  return next(specifier, context);
}
