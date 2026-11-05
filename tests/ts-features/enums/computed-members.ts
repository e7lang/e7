// @feature: Computed and constant-expression enum members
// @category: Enums
// @status: unsupported
// @why: not implemented yet: initializers must be number or string literals
// @instead: write the value as a literal (`Write = 2`), or use `const` variables
// @error: enum member initializers must be number or string literals
enum FileAccess { None, Read = 1 << 1, Write = 1 << 2, ReadWrite = Read | Write, G = "123".length }
console.log(FileAccess.ReadWrite, FileAccess.G);
