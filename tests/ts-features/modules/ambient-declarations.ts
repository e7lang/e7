// @feature: Ambient variable and class declarations (`declare const`, `declare class`)
// @category: Modules
// @status: unsupported
// @why: verifiable bytecode: only functions can be provided by the host, with a signature the VM checks when the module is loaded
// @instead: a host function that returns the value: `declare function appVersion(): string` (host functions are E7's form of `declare`, docs/SPEC.md section 7; they need a host that registers them, so they have no sample here)
// @error: only 'declare function' is supported
declare const APP_VERSION: string;
console.log(typeof APP_VERSION);
