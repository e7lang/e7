// A class whose constructor parameter types are declared in this module only (not exported).
interface Def { name: string; tags: string[] }
export class Graph {
  readonly label: string;
  constructor(d: Def, extra: Opts = { sep: "/" }) { this.label = d.name + extra.sep + d.tags.join(","); }
}
interface Opts { sep: string }
