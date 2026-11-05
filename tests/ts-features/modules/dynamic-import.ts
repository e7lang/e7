// @feature: Dynamic `import()`
// @category: Modules
// @status: unsupported
// @why: verifiable bytecode: every module is compiled and verified ahead of time; `import()` also returns a Promise
// @instead: static imports; the host loads modules
// @error: 'import' is a reserved word
const mod = import("./_lib/counter.ts");
console.log(typeof mod);
