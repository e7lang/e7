// @feature: Enum and number / string compatibility
// @category: Type Compatibility and Inference
// @status: supported
enum Status { Ready, Waiting }
enum Mode { On = "on" }
function code(n: number): string { return "#" + n; }
function text(s: string): string { return s + "!"; }
console.log(code(Status.Waiting), text(Mode.On), Status.Ready === 0);
