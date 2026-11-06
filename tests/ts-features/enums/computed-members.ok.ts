// @feature: Computed and constant-expression enum members
// @category: Enums
// @status: partial
// @note: constant expressions: literals, other members, `+ - * / % ** | & ^ << >> >>> ~`, string concatenation
enum FileAccess { None, Read = 1 << 1, Write = 1 << 2, ReadWrite = Read | Write }
enum Prefix { A = "x", B = A + "-b" }
console.log(FileAccess.ReadWrite, Prefix.B);
