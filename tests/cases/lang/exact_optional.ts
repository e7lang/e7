// exactOptionalPropertyTypes (SPEC section 0.1): 'x?: T' may be left out but not set to undefined;
// 'x?: T | undefined' may be set to undefined
interface Opts { name: string; retries?: number; label?: string | undefined }
function label(o: Opts): string {
  return o.name + ":" + (o.retries ?? 0) + ":" + (o.label ?? "-");
}
const maybe: number | undefined = [3, 4].find((n) => n > 3);
const a: Opts = { name: "a" };
const b: Opts = { name: "b", retries: maybe ?? 1, label: undefined };
if (maybe !== undefined) a.retries = maybe;  // narrowed: never undefined
b.label = "x";
b.label = undefined;
const c: Opts = { ...a, name: "c" };  // a.retries is optional: absent stays absent
console.log(label(a), label(b), label(c), JSON.stringify(c));

class Job {
  timeout?: number;
  owner?: string | undefined;
  constructor(public id: number, public tag?: string) {}
}
const j = new Job(1);
j.timeout = 30;
j.owner = undefined;
console.log(j.id, j.timeout, j.owner ?? "none", j.tag ?? "untagged");
