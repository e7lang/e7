// @error: because it is a read-only property
class A { static readonly LIMIT: number = 3; }
class B extends A {}
B.LIMIT = 4;
