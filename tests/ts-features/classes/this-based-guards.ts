// @feature: `this`-based type guards (`this is T`)
// @category: Classes
// @status: unsupported
// @why: not implemented yet: user-defined type guards are not parsed
// @instead: `instanceof`
// @error: type predicates ('x is T') are not supported
class FileSystemObject {
  isFile(): this is FileRep { return this instanceof FileRep; }
}
class FileRep extends FileSystemObject {
  content = "data";
}
const fso: FileSystemObject = new FileRep();
if (fso.isFile()) console.log(fso.content);
