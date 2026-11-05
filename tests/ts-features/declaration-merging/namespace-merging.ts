// @feature: Merging namespaces with classes, functions and enums
// @category: Declaration Merging
// @status: unsupported
// @why: not implemented yet: namespaces are not supported
// @instead: static members of the class, or module-level declarations
// @error: namespaces are not supported
class Album { label = "x"; }
namespace Album {
  export const defaultLabel = "unknown";
}
function buildLabel(name: string): string { return buildLabel.prefix + name; }
namespace buildLabel {
  export const prefix = "Hello, ";
}
console.log(Album.defaultLabel, buildLabel("Ann"));
