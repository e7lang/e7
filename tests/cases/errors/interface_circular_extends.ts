// @error: circularly extends itself
interface A extends B { x: number }
interface B extends A { y: number }
