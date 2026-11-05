// @feature: Ambient namespaces (`declare namespace`)
// @category: Namespaces
// @status: unsupported
// @why: verifiable bytecode: only functions can be provided by the host
// @instead: host functions declared with `declare function`
// @error: only 'declare function' is supported
declare namespace D3 {
  function select(selector: string): string;
}
console.log(typeof D3);
