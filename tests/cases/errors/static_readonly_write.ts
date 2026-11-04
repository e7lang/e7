// @error: because it is a read-only property
class C { static readonly LIMIT: number = 3; }
C.LIMIT++;
