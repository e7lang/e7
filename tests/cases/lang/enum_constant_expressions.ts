// constant expressions in enum members: earlier members, other enums, unary and binary operators, string concatenation
enum FileAccess { None, Read = 1 << 1, Write = 1 << 2, ReadWrite = Read | Write, All = ~(~0 << 3), Neg = -FileAccess.Write * 2 }
enum Mask { Low = 0xff, High = Low << 8, Both = Low | High, Top = 1 << 31, Shifted = -1 >>> 28, Mod = 17 % 5, Pow = 2 ** 10 }
enum Prefix { A = "x", B = A + "-b", C = "c" + "d", D = `tpl` }
enum Next { First = Mask.Low + 1, Second }
console.log(FileAccess.ReadWrite, FileAccess.All, FileAccess.Neg, Mask.Both, Mask.Top, Mask.Shifted, Mask.Mod, Mask.Pow, Prefix.B, Prefix.C, Prefix.D, Next.Second);
