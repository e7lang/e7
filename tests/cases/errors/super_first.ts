// @error: must call super
class A { constructor(public a: number) {} }
class B extends A { constructor() { console.log(1); super(1); } }
