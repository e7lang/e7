// a star of a module that has star exports itself, and a namespace re-exported by name
export * from "./reexport_hub.ts";
import * as B from "./reexport_b.ts";
export { B };
