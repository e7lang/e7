// @error: because it is a read-only property
class A { readonly x: number = 1; }
class B extends A { constructor() { super(); this.x = 2; } }
