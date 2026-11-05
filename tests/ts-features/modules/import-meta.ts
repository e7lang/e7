// @feature: `import.meta`
// @category: Modules
// @status: unsupported
// @why: not implemented yet: the host, not the script, knows where modules come from
// @instead: pass the information in from the host (a host function or an argument)
// @error: 'import' is a reserved word
console.log(typeof import.meta.url);
