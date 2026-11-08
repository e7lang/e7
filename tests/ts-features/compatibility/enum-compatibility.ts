// @feature: Enum and number / string compatibility
// @category: Type Compatibility and Inference
// @status: supported
// @note: A `number` converts to a numeric enum (`bytes[0] as Kind`, or assigned); a number constant must be one of its values
enum Status { Ready, Waiting }
enum Mode { On = "on" }
function code(n: number): string { return "#" + n; }
function text(s: string): string { return s + "!"; }
console.log(code(Status.Waiting), text(Mode.On), Status.Ready === 0);
const wire = Uint8Array.of(1, 7);
const k = wire[0] as Status;
const n: number = wire[1];
const unknown: Status = n;
console.log(k === Status.Waiting, unknown);
